/*
 * Widget Nexus — GTK 3 / Ubuntu-oriented port.
 * Uses the same widgets.txt format as the Win32 build (see repo root).
 */

#include "model.hpp"

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <cairo.h>

#ifdef HAVE_AYATANA_APPINDICATOR
#include <libayatana-appindicator/app-indicator.h>
#endif

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char kAppVersion[] = "0.2.0-lnx";
constexpr const char kRepoUrl[] = "https://github.com/robbinc91/widget-nexus";

constexpr int kFloaterDiameter = 76;
constexpr int kFloaterDragRingPx = 12;
constexpr int kFloaterGap = 8;
constexpr int kFloaterMargin = 10;
constexpr guint kFloaterAlpha = 200;
constexpr guint kFloaterAnimIntervalMs = 22;
constexpr guint kGroupPulseLowAlpha = 118;

struct WidgetRowGtk {
    Widget w;
    GtkWidget* floater = nullptr;
};

struct GroupRowGtk {
    Group g;
    GtkWidget* floater = nullptr;
};

enum class AnimKind : unsigned char { FadeIn, FadeOut, PulseDown, PulseUp };

struct FloaterAnimGtk {
    GtkWidget* win = nullptr;
    guint alpha = 0;
    AnimKind kind = AnimKind::FadeIn;
};

struct NexusApp;

struct FloaterCtx {
    NexusApp* app = nullptr;
    int index = -1;
    bool is_group = false;
};

struct NexusApp {
    GtkApplication* gtkApp = nullptr;

    std::vector<WidgetRowGtk> rows;
    std::vector<GroupRowGtk> groups;
    bool showNonPinned = true;
    int selWidget = -1;
    int selGroup = -1;
    std::string configPath;

    GtkWidget* window = nullptr;
    GtkWidget* listWidgets = nullptr;
    GtkWidget* listGroups = nullptr;
    GtkWidget* entryName = nullptr;
    GtkWidget* comboGroup = nullptr;
    GtkWidget* checkPinned = nullptr;
    GtkWidget* tvCommands = nullptr;
    GtkTextBuffer* bufCommands = nullptr;
    GtkWidget* entryGroupName = nullptr;
    GtkWidget* checkGroupPin = nullptr;
    GtkWidget* chkShowNonPinned = nullptr;
    GtkWidget* lblStatus = nullptr;

    guint timerFloater = 0;
    std::vector<FloaterAnimGtk> anims;

#ifdef HAVE_AYATANA_APPINDICATOR
    AppIndicator* indicator = nullptr;
#endif
};

constexpr double kCx = 0.0 / 255.0, kCy = 220 / 255.0, kCz = 235 / 255.0;
constexpr double kMx = 235 / 255.0, kMy = 72 / 255.0, kMz = 198 / 255.0;
constexpr double kBgR = 12 / 255.0, kBgG = 16 / 255.0, kBb = 34 / 255.0;

inline NexusApp* NA(gpointer p) { return static_cast<NexusApp*>(p); }

static void LayoutFloaters(NexusApp* app);
static void SyncFloaterVisibility(NexusApp* app, bool animate);
static void RefreshLists(NexusApp* app);

static int FindGroupIndex(const NexusApp* app, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < app->groups.size(); ++i) {
        if (app->groups[i].g.name == name) return static_cast<int>(i);
    }
    return -1;
}

static bool WidgetVisible(const NexusApp* app, size_t i) {
    if (i >= app->rows.size()) return false;
    const WidgetRowGtk& row = app->rows[i];
    if (row.w.groupName.empty()) return row.w.alwaysVisible || app->showNonPinned;
    const int gi = FindGroupIndex(app, row.w.groupName);
    if (gi >= 0) return app->groups[static_cast<size_t>(gi)].g.visible;
    return row.w.alwaysVisible || app->showNonPinned;
}

static bool WidgetOccupiesSlot(const NexusApp* app, size_t i) {
    if (!app->rows[i].floater) return false;
    if (WidgetVisible(app, i)) return true;
    for (const FloaterAnimGtk& a : app->anims) {
        if (a.win == app->rows[i].floater && a.kind == AnimKind::FadeOut) return true;
    }
    return false;
}

static void SetStatus(NexusApp* app, const std::string& msg) {
    if (app->lblStatus) gtk_label_set_text(GTK_LABEL(app->lblStatus), msg.c_str());
}

static void ApplyCircleShape(GtkWidget* widget) {
    GdkWindow* gw = gtk_widget_get_window(widget);
    if (!gw) return;
    const gint w = gtk_widget_get_allocated_width(widget);
    const gint h = gtk_widget_get_allocated_height(widget);
    if (w < 8 || h < 8) return;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A1, w, h);
    cairo_t* cr = cairo_create(surface);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double R = std::min(w, h) / 2.0 - 1.0;
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_destroy(cr);

    cairo_region_t* region = gdk_cairo_region_create_from_surface(surface);
    cairo_surface_destroy(surface);
    gdk_window_shape_combine_region(gw, region, 0, 0);
    cairo_region_destroy(region);
}

static gboolean floater_configure(GtkWidget* widget, GdkEventConfigure*, gpointer) {
    ApplyCircleShape(widget);
    return FALSE;
}

static void FloaterSetOpacity(GtkWidget* win, guint alpha) {
    GdkWindow* gw = gtk_widget_get_window(win);
    if (gw) gdk_window_set_opacity(gw, std::min(255u, alpha) / 255.0);
}

static void DestroyListChildren(GtkWidget* list) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* l = children; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
}

static void RemoveAnimFor(NexusApp* app, GtkWidget* win) {
    app->anims.erase(std::remove_if(app->anims.begin(), app->anims.end(),
                         [win](const FloaterAnimGtk& a) { return a.win == win; }),
        app->anims.end());
}

static void PushFadeIn(NexusApp* app, GtkWidget* win) {
    RemoveAnimFor(app, win);
    FloaterAnimGtk a{};
    a.win = win;
    a.alpha = 0;
    a.kind = AnimKind::FadeIn;
    app->anims.push_back(a);
    gtk_widget_show_all(win);
    FloaterSetOpacity(win, 0);
}

static void PushFadeOut(NexusApp* app, GtkWidget* win) {
    RemoveAnimFor(app, win);
    FloaterAnimGtk a{};
    a.win = win;
    a.alpha = kFloaterAlpha;
    a.kind = AnimKind::FadeOut;
    app->anims.push_back(a);
    FloaterSetOpacity(win, kFloaterAlpha);
}

static void PushGroupPulse(NexusApp* app, GtkWidget* win) {
    RemoveAnimFor(app, win);
    FloaterAnimGtk a{};
    a.win = win;
    a.alpha = kFloaterAlpha;
    a.kind = AnimKind::PulseDown;
    app->anims.push_back(a);
}

static gboolean TickFloaterAnimations(gpointer data) {
    auto* app = NA(data);
    bool layoutNeeded = false;
    const int step = std::max(8, static_cast<int>(kFloaterAlpha) / 8);

    for (size_t i = 0; i < app->anims.size();) {
        FloaterAnimGtk& a = app->anims[i];
        if (!a.win || !GTK_IS_WIDGET(a.win)) {
            app->anims.erase(app->anims.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        switch (a.kind) {
        case AnimKind::FadeIn: {
            const int next = std::min(static_cast<int>(kFloaterAlpha), static_cast<int>(a.alpha) + step);
            a.alpha = static_cast<guint>(next);
            FloaterSetOpacity(a.win, a.alpha);
            if (a.alpha >= kFloaterAlpha) {
                app->anims.erase(app->anims.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            break;
        }
        case AnimKind::FadeOut: {
            const int next = std::max(0, static_cast<int>(a.alpha) - step);
            a.alpha = static_cast<guint>(next);
            FloaterSetOpacity(a.win, a.alpha);
            if (a.alpha <= 0) {
                gtk_widget_hide(a.win);
                FloaterSetOpacity(a.win, kFloaterAlpha);
                app->anims.erase(app->anims.begin() + static_cast<std::ptrdiff_t>(i));
                layoutNeeded = true;
                continue;
            }
            break;
        }
        case AnimKind::PulseDown: {
            if (static_cast<int>(a.alpha) - step <= static_cast<int>(kGroupPulseLowAlpha)) {
                a.alpha = kGroupPulseLowAlpha;
                a.kind = AnimKind::PulseUp;
            } else {
                a.alpha = static_cast<guint>(static_cast<int>(a.alpha) - step);
            }
            FloaterSetOpacity(a.win, a.alpha);
            break;
        }
        case AnimKind::PulseUp: {
            const int next = std::min(static_cast<int>(kFloaterAlpha), static_cast<int>(a.alpha) + step);
            a.alpha = static_cast<guint>(next);
            FloaterSetOpacity(a.win, a.alpha);
            if (a.alpha >= kFloaterAlpha) {
                app->anims.erase(app->anims.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            break;
        }
        }
        ++i;
    }

    if (layoutNeeded) LayoutFloaters(app);

    if (app->anims.empty()) {
        app->timerFloater = 0;
        return FALSE;
    }
    return TRUE;
}

static void EnsureFloaterTimer(NexusApp* app) {
    if (app->timerFloater != 0 || app->anims.empty()) return;
    app->timerFloater = g_timeout_add(kFloaterAnimIntervalMs, TickFloaterAnimations, app);
}

static void destroy_fctx(GtkWidget*, gpointer p) {
    delete reinterpret_cast<FloaterCtx*>(p);
}

static gboolean draw_widget_floater(GtkWidget* da, cairo_t* cr, gpointer ud) {
    auto* ctx = static_cast<FloaterCtx*>(ud);
    NexusApp* na = ctx->app;
    const int idx = ctx->index;
    std::string name = "Widget";
    if (idx >= 0 && static_cast<size_t>(idx) < na->rows.size()) name = na->rows[static_cast<size_t>(idx)].w.name;

    const gint ww = gtk_widget_get_allocated_width(da);
    const gint hh = gtk_widget_get_allocated_height(da);
    const double cx = ww / 2.0;
    const double cy = hh / 2.0;
    const double R = std::min(ww, hh) / 2.0 - 2.0;

    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, kBgR, kBgG, kBb);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, kCx, kCy, kCz);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    cairo_arc(cr, cx, cy, R - 1.2, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, kMx * 0.4, kMy * 0.4, kMz * 0.4, 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_set_source_rgb(cr, kCx, kCy, kCz);
    cairo_text_extents_t ext{};
    cairo_text_extents(cr, name.c_str(), &ext);
    cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, cy - ext.height / 2.0 - ext.y_bearing);
    cairo_show_text(cr, name.c_str());

    return FALSE;
}

static gboolean draw_group_floater(GtkWidget* da, cairo_t* cr, gpointer ud) {
    auto* ctx = static_cast<FloaterCtx*>(ud);
    NexusApp* na = ctx->app;
    const int idx = ctx->index;
    bool vis = false;
    std::string title = "Group";
    if (idx >= 0 && static_cast<size_t>(idx) < na->groups.size()) {
        vis = na->groups[static_cast<size_t>(idx)].g.visible;
        title = na->groups[static_cast<size_t>(idx)].g.name;
    }

    const gint ww = gtk_widget_get_allocated_width(da);
    const gint hh = gtk_widget_get_allocated_height(da);
    const double cx = ww / 2.0;
    const double cy = hh / 2.0;
    const double R = std::min(ww, hh) / 2.0 - 2.0;

    if (vis)
        cairo_set_source_rgb(cr, 18 / 255.0, 52 / 255.0, 34 / 255.0);
    else
        cairo_set_source_rgb(cr, 52 / 255.0, 28 / 255.0, 30 / 255.0);

    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, kCx, kCy, kCz);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);
    cairo_set_source_rgb(cr, kCx, kCy, kCz);

    std::vector<std::string> lines;
    std::string chunk;
    for (char c : title) {
        if (chunk.size() > 14) {
            lines.push_back(chunk);
            chunk.clear();
        }
        chunk += c;
    }
    if (!chunk.empty()) lines.push_back(chunk);
    if (lines.empty()) lines.push_back("Grp");

    double y = cy - (static_cast<int>(lines.size()) - 1) * 6.0;
    for (const auto& ln : lines) {
        cairo_text_extents_t ext{};
        cairo_text_extents(cr, ln.c_str(), &ext);
        cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, y - ext.height / 2.0 - ext.y_bearing);
        cairo_show_text(cr, ln.c_str());
        y += 13.0;
    }

    return FALSE;
}

static gboolean widget_floater_press(GtkWidget* da, GdkEventButton* ev, gpointer /*user_data*/) {
    GtkWidget* win = gtk_widget_get_toplevel(da);
    const gint w = gtk_widget_get_allocated_width(da);
    const gint h = gtk_widget_get_allocated_height(da);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double dx = ev->x - cx;
    const double dy = ev->y - cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double R = std::min(w, h) / 2.0;
    const double rInner = std::max(1.0, R - kFloaterDragRingPx);
    if (dist >= rInner && dist <= R) {
        gtk_window_begin_move_drag(GTK_WINDOW(win), ev->button, static_cast<int>(ev->x_root), static_cast<int>(ev->y_root),
            ev->time);
        return TRUE;
    }
    return FALSE;
}

static gboolean widget_floater_release(GtkWidget* da, GdkEventButton* ev, gpointer user_data) {
    auto* ctx = static_cast<FloaterCtx*>(user_data);
    NexusApp* app = ctx->app;
    const gint w = gtk_widget_get_allocated_width(da);
    const gint h = gtk_widget_get_allocated_height(da);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double evx = ev ? ev->x : cx;
    const double evy = ev ? ev->y : cy;
    const double dx = evx - cx;
    const double dy = evy - cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double R = std::min(w, h) / 2.0;
    const double rInner = std::max(1.0, R - kFloaterDragRingPx);
    if (dist < rInner && ctx->index >= 0 && static_cast<size_t>(ctx->index) < app->rows.size()) {
        const Widget& ww = app->rows[static_cast<size_t>(ctx->index)].w;
        if (ww.commands.empty()) {
            SetStatus(app, "No commands to run.");
            return TRUE;
        }
        SetStatus(app, std::string("Running: ") + ww.name);
        for (const auto& cmd : ww.commands) {
            std::string out;
            if (!RunSingleCommand(cmd, out)) {
                SetStatus(app, std::string("Failed: ") + ww.name);
                return TRUE;
            }
        }
        SetStatus(app, std::string("Completed: ") + ww.name);
    }
    return FALSE;
}

static gboolean group_floater_press(GtkWidget* da, GdkEventButton* ev, gpointer /*user_data*/) {
    GtkWidget* win = gtk_widget_get_toplevel(da);
    const gint w = gtk_widget_get_allocated_width(da);
    const gint h = gtk_widget_get_allocated_height(da);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double dx = ev->x - cx;
    const double dy = ev->y - cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double R = std::min(w, h) / 2.0;
    const double rInner = std::max(1.0, R - kFloaterDragRingPx);
    if (dist >= rInner && dist <= R) {
        gtk_window_begin_move_drag(GTK_WINDOW(win), ev->button, static_cast<int>(ev->x_root), static_cast<int>(ev->y_root),
            ev->time);
        return TRUE;
    }
    return FALSE;
}

static gboolean group_floater_release(GtkWidget* da, GdkEventButton* ev, gpointer user_data) {
    auto* ctx = static_cast<FloaterCtx*>(user_data);
    NexusApp* app = ctx->app;
    const gint w = gtk_widget_get_allocated_width(da);
    const gint h = gtk_widget_get_allocated_height(da);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double evx = ev ? ev->x : cx;
    const double evy = ev ? ev->y : cy;
    const double dx = evx - cx;
    const double dy = evy - cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double R = std::min(w, h) / 2.0;
    const double rInner = std::max(1.0, R - kFloaterDragRingPx);
    if (dist < rInner && ctx->index >= 0 && static_cast<size_t>(ctx->index) < app->groups.size()) {
        GroupRowGtk& gr = app->groups[static_cast<size_t>(ctx->index)];
        gr.g.visible = !gr.g.visible;
        if (gr.floater) {
            gtk_widget_queue_draw(gr.floater);
            PushGroupPulse(app, gr.floater);
            EnsureFloaterTimer(app);
        }
        SyncFloaterVisibility(app, true);
        LayoutFloaters(app);
    }
    return FALSE;
}

static void ApplyWidgetTopmost(NexusApp* app, size_t i) {
    if (i >= app->rows.size() || !app->rows[i].floater) return;
    gtk_window_set_keep_above(GTK_WINDOW(app->rows[i].floater), app->rows[i].w.alwaysVisible ? TRUE : FALSE);
}

static void ApplyGroupTopmost(NexusApp* app, size_t i) {
    if (i >= app->groups.size() || !app->groups[i].floater) return;
    gtk_window_set_keep_above(GTK_WINDOW(app->groups[i].floater), app->groups[i].g.alwaysVisible ? TRUE : FALSE);
}

static GtkWidget* MakeWidgetFloater(NexusApp* app, size_t idx) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_widget_set_size_request(win, kFloaterDiameter, kFloaterDiameter);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_UTILITY);

    gtk_widget_set_app_paintable(win, TRUE);
    GdkScreen* scr = gtk_widget_get_screen(win);
    GdkVisual* vis = gdk_screen_get_rgba_visual(scr);
    if (vis) gtk_widget_set_visual(win, vis);

    GtkWidget* da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, kFloaterDiameter, kFloaterDiameter);
    gtk_container_add(GTK_CONTAINER(win), da);

    auto* ctx = new FloaterCtx{app, static_cast<int>(idx), false};
    g_signal_connect(win, "destroy", G_CALLBACK(destroy_fctx), ctx);

    gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(da, "draw", G_CALLBACK(draw_widget_floater), ctx);
    g_signal_connect(da, "button-press-event", G_CALLBACK(widget_floater_press), ctx);
    g_signal_connect(da, "button-release-event", G_CALLBACK(widget_floater_release), ctx);

    g_signal_connect(win, "configure-event", G_CALLBACK(floater_configure), nullptr);

    gtk_widget_show_all(win);
    gtk_widget_hide(win);

    return win;
}

static GtkWidget* MakeGroupFloater(NexusApp* app, size_t idx) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_widget_set_size_request(win, kFloaterDiameter, kFloaterDiameter);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_UTILITY);

    gtk_widget_set_app_paintable(win, TRUE);
    GdkScreen* scr = gtk_widget_get_screen(win);
    GdkVisual* vis = gdk_screen_get_rgba_visual(scr);
    if (vis) gtk_widget_set_visual(win, vis);

    GtkWidget* da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, kFloaterDiameter, kFloaterDiameter);
    gtk_container_add(GTK_CONTAINER(win), da);

    auto* ctx = new FloaterCtx{app, static_cast<int>(idx), true};
    g_signal_connect(win, "destroy", G_CALLBACK(destroy_fctx), ctx);

    gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(da, "draw", G_CALLBACK(draw_group_floater), ctx);
    g_signal_connect(da, "button-press-event", G_CALLBACK(group_floater_press), ctx);
    g_signal_connect(da, "button-release-event", G_CALLBACK(group_floater_release), ctx);

    g_signal_connect(win, "configure-event", G_CALLBACK(floater_configure), nullptr);

    gtk_widget_show_all(win);
    FloaterSetOpacity(win, kFloaterAlpha);
    return win;
}

static void DestroyAllFloaters(NexusApp* app) {
    app->anims.clear();
    if (app->timerFloater != 0) {
        g_source_remove(app->timerFloater);
        app->timerFloater = 0;
    }
    for (auto& r : app->rows) {
        if (r.floater) {
            gtk_widget_destroy(r.floater);
            r.floater = nullptr;
        }
    }
    for (auto& g : app->groups) {
        if (g.floater) {
            gtk_widget_destroy(g.floater);
            g.floater = nullptr;
        }
    }
}

static void SyncFloaterVisibility(NexusApp* app, bool animate) {
    if (!animate) {
        app->anims.clear();
        if (app->timerFloater != 0) {
            g_source_remove(app->timerFloater);
            app->timerFloater = 0;
        }
        for (size_t i = 0; i < app->rows.size(); ++i) {
            WidgetRowGtk& r = app->rows[i];
            if (!r.floater) continue;
            const bool want = WidgetVisible(app, i);
            if (want) {
                gtk_widget_show_all(r.floater);
                FloaterSetOpacity(r.floater, kFloaterAlpha);
            } else {
                gtk_widget_hide(r.floater);
                FloaterSetOpacity(r.floater, kFloaterAlpha);
            }
        }
        for (auto& g : app->groups) {
            if (!g.floater) continue;
            gtk_widget_show_all(g.floater);
            FloaterSetOpacity(g.floater, kFloaterAlpha);
        }
        return;
    }

    for (size_t i = 0; i < app->rows.size(); ++i) {
        WidgetRowGtk& r = app->rows[i];
        if (!r.floater) continue;
        const bool want = WidgetVisible(app, i);
        const gboolean shown = gtk_widget_get_visible(r.floater);
        if (want) {
            RemoveAnimFor(app, r.floater);
            if (!shown) PushFadeIn(app, r.floater);
            else {
                gtk_widget_show_all(r.floater);
                FloaterSetOpacity(r.floater, kFloaterAlpha);
            }
        } else {
            if (shown) PushFadeOut(app, r.floater);
        }
    }
    for (auto& g : app->groups) {
        if (!g.floater) continue;
        gtk_widget_show_all(g.floater);
        bool hasAnim = false;
        for (const auto& a : app->anims) {
            if (a.win == g.floater) {
                hasAnim = true;
                break;
            }
        }
        if (!hasAnim) FloaterSetOpacity(g.floater, kFloaterAlpha);
    }
    EnsureFloaterTimer(app);
}

static GdkMonitor* PrimaryMonitor() {
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return nullptr;
    GdkMonitor* m = gdk_display_get_primary_monitor(d);
    if (!m) m = gdk_display_get_monitor(d, 0);
    return m;
}

static void LayoutFloaters(NexusApp* app) {
    GdkMonitor* mon = PrimaryMonitor();
    GdkRectangle work{};
    if (!mon) {
        work.x = 0;
        work.y = 0;
        work.width = 1920;
        work.height = 1080;
    } else {
        gdk_monitor_get_workarea(mon, &work);
    }

    const int usableH = std::max(1, work.height - 2 * kFloaterMargin);
    const int colStride = kFloaterDiameter + kFloaterGap;
    const int rowStride = colStride;
    const int rowsPerCol = std::max(1, usableH / rowStride);
    const size_t rowsPerColumn = static_cast<size_t>(rowsPerCol);
    size_t slotIndex = 0;

    const auto placeFloater = [&](GtkWidget* floater, int diameter) {
        if (!floater) return;
        const int col = static_cast<int>(slotIndex / rowsPerColumn);
        const int row = static_cast<int>(slotIndex % rowsPerColumn);
        const int x = work.x + work.width - kFloaterMargin - diameter - col * colStride;
        const int y = work.y + kFloaterMargin + row * rowStride;
        gtk_window_move(GTK_WINDOW(floater), x, y);
        ++slotIndex;
    };

    for (size_t gi = 0; gi < app->groups.size(); ++gi) {
        placeFloater(app->groups[gi].floater, kFloaterDiameter);
        for (size_t wi = 0; wi < app->rows.size(); ++wi) {
            if (app->rows[wi].w.groupName != app->groups[gi].g.name) continue;
            if (!WidgetOccupiesSlot(app, wi)) continue;
            placeFloater(app->rows[wi].floater, kFloaterDiameter);
        }
    }

    for (size_t wi = 0; wi < app->rows.size(); ++wi) {
        if (FindGroupIndex(app, app->rows[wi].w.groupName) >= 0) continue;
        if (!WidgetOccupiesSlot(app, wi)) continue;
        placeFloater(app->rows[wi].floater, kFloaterDiameter);
    }
}

static void ReadEditorToWidget(NexusApp* app, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(app->rows.size())) return;
    Widget& w = app->rows[static_cast<size_t>(modelIndex)].w;
    w.name = Trim(gtk_entry_get_text(GTK_ENTRY(app->entryName)));
    w.alwaysVisible = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->checkPinned)) != FALSE;
    const int groupSel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->comboGroup));
    if (groupSel <= 0)
        w.groupName.clear();
    else if (groupSel - 1 < static_cast<int>(app->groups.size()))
        w.groupName = app->groups[static_cast<size_t>(groupSel - 1)].g.name;

    w.commands.clear();
    GtkTextIter s0, s1;
    gtk_text_buffer_get_start_iter(app->bufCommands, &s0);
    gtk_text_buffer_get_end_iter(app->bufCommands, &s1);
    gchar* txt = gtk_text_buffer_get_text(app->bufCommands, &s0, &s1, FALSE);
    std::istringstream iss(txt ? txt : "");
    if (txt) g_free(txt);
    std::string line;
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (!line.empty()) w.commands.push_back(line);
    }
}

static void WriteWidgetToEditor(NexusApp* app, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(app->rows.size())) {
        gtk_entry_set_text(GTK_ENTRY(app->entryName), "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->checkPinned), FALSE);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->comboGroup), 0);
        gtk_text_buffer_set_text(app->bufCommands, "", -1);
        return;
    }
    const Widget& w = app->rows[static_cast<size_t>(modelIndex)].w;
    gtk_entry_set_text(GTK_ENTRY(app->entryName), w.name.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->checkPinned), w.alwaysVisible ? TRUE : FALSE);
    const int gi = FindGroupIndex(app, w.groupName);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->comboGroup), gi >= 0 ? gi + 1 : 0);

    std::ostringstream oss;
    for (size_t i = 0; i < w.commands.size(); ++i) {
        oss << w.commands[i];
        if (i + 1 < w.commands.size()) oss << '\n';
    }
    gtk_text_buffer_set_text(app->bufCommands, oss.str().c_str(), -1);
}

static void ReadEditorToGroup(NexusApp* app, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(app->groups.size())) return;
    Group& g = app->groups[static_cast<size_t>(groupIndex)].g;
    const std::string oldName = g.name;
    const std::string newName = Trim(gtk_entry_get_text(GTK_ENTRY(app->entryGroupName)));
    if (!newName.empty()) g.name = newName;
    g.alwaysVisible = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->checkGroupPin)) != FALSE;
    if (oldName != g.name) {
        for (auto& r : app->rows) {
            if (r.w.groupName == oldName) r.w.groupName = g.name;
        }
    }
}

static void WriteGroupToEditor(NexusApp* app, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(app->groups.size())) {
        gtk_entry_set_text(GTK_ENTRY(app->entryGroupName), "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->checkGroupPin), FALSE);
        return;
    }
    const Group& g = app->groups[static_cast<size_t>(groupIndex)].g;
    gtk_entry_set_text(GTK_ENTRY(app->entryGroupName), g.name.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->checkGroupPin), g.alwaysVisible ? TRUE : FALSE);
}

static void RefreshGroupCombo(NexusApp* app) {
    const int prev = gtk_combo_box_get_active(GTK_COMBO_BOX(app->comboGroup));
    std::string prevName;
    if (prev > 0 && prev - 1 < static_cast<int>(app->groups.size())) prevName = app->groups[static_cast<size_t>(prev - 1)].g.name;

    GtkComboBoxText* cbt = GTK_COMBO_BOX_TEXT(app->comboGroup);
    GtkTreeModel* model = gtk_combo_box_get_model(GTK_COMBO_BOX(cbt));
    while (model && gtk_tree_model_iter_n_children(model, nullptr) > 0) gtk_combo_box_text_remove(cbt, 0);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->comboGroup), "(no group)");
    for (const auto& gr : app->groups) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->comboGroup), gr.g.name.c_str());

    if (!prevName.empty()) {
        const int idx = FindGroupIndex(app, prevName);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->comboGroup), idx >= 0 ? idx + 1 : 0);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->comboGroup), 0);
    }
}

static GtkWidget* MakeListRowLabel(const std::string& text, int modelIndex) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* lbl = gtk_label_new(text.c_str());
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(row), lbl);
    g_object_set_data(G_OBJECT(row), "nx-idx", GINT_TO_POINTER(modelIndex));
    return row;
}

static void RefreshLists(NexusApp* app) {
    DestroyListChildren(app->listWidgets);
    for (size_t i = 0; i < app->rows.size(); ++i) {
        const Widget& w = app->rows[i].w;
        std::string label = (w.alwaysVisible ? "[PIN] " : "[   ] ") + w.name;
        if (!w.groupName.empty()) label += " {" + w.groupName + "}";
        gtk_container_add(GTK_CONTAINER(app->listWidgets), MakeListRowLabel(label, static_cast<int>(i)));
    }

    DestroyListChildren(app->listGroups);
    for (size_t i = 0; i < app->groups.size(); ++i) {
        const Group& g = app->groups[i].g;
        std::string label = (g.visible ? "[ON ] " : "[OFF] ") + g.name + (g.alwaysVisible ? " [PIN]" : "");
        gtk_container_add(GTK_CONTAINER(app->listGroups), MakeListRowLabel(label, static_cast<int>(i)));
    }

    gtk_widget_show_all(app->listWidgets);
    gtk_widget_show_all(app->listGroups);

    if (app->selWidget >= 0 && static_cast<size_t>(app->selWidget) < app->rows.size()) {
        /* gtk_list_box_select_row requires row widget — skip precise restore for simplicity */
        WriteWidgetToEditor(app, app->selWidget);
    } else {
        app->selWidget = -1;
        WriteWidgetToEditor(app, -1);
    }

    if (app->selGroup >= 0 && static_cast<size_t>(app->selGroup) < app->groups.size())
        WriteGroupToEditor(app, app->selGroup);
    else {
        app->selGroup = -1;
        WriteGroupToEditor(app, -1);
    }

    RefreshGroupCombo(app);
}

static void SaveCurrentEditorWidget(NexusApp* app) {
    if (app->selWidget >= 0 && app->selWidget < static_cast<int>(app->rows.size()))
        ReadEditorToWidget(app, app->selWidget);
}

static void RebuildFloaters(NexusApp* app) {
    DestroyAllFloaters(app);
    for (size_t i = 0; i < app->rows.size(); ++i) {
        app->rows[i].floater = MakeWidgetFloater(app, i);
        ApplyWidgetTopmost(app, i);
    }
    for (size_t i = 0; i < app->groups.size(); ++i) {
        app->groups[i].floater = MakeGroupFloater(app, i);
        ApplyGroupTopmost(app, i);
    }
    SyncFloaterVisibility(app, false);
    LayoutFloaters(app);
}

static void RunWidget(NexusApp* app, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(app->rows.size())) return;
    const Widget& w = app->rows[static_cast<size_t>(modelIndex)].w;
    if (w.commands.empty()) {
        SetStatus(app, "No commands to run.");
        return;
    }
    SetStatus(app, std::string("Running: ") + w.name);
    for (const auto& cmd : w.commands) {
        std::string out;
        if (!RunSingleCommand(cmd, out)) {
            SetStatus(app, std::string("Failed: ") + w.name);
            return;
        }
    }
    SetStatus(app, std::string("Completed: ") + w.name);
}

static void RemoveGroupAssignment(NexusApp* app, const std::string& groupName) {
    if (groupName.empty()) return;
    for (auto& row : app->rows) {
        if (row.w.groupName == groupName) row.w.groupName.clear();
    }
}

static void ApplyDarkCss() {
    GtkCssProvider* css = gtk_css_provider_new();
    const char* data =
        "* { color: #e0f6ff; font-family: sans-serif; }\n"
        "window { background-color: #080a14; }\n"
        "entry, textview, comboboxtext { background-color: #0c1022; }\n"
        "button { background-image: none; background-color: #182442; padding: 6px 12px; }\n"
        "list { background-color: #10142a; }\n"
        "label#status { background-color: #0a0e1e; padding: 6px; }\n";
    gtk_css_provider_load_from_data(css, data, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

static gboolean on_display_changed(GdkScreen*, gpointer user_data) {
    LayoutFloaters(NA(user_data));
    return FALSE;
}

static void on_widget_sel(GtkListBox* lb, GtkListBoxRow* row, gpointer user_data);
static void on_group_sel(GtkListBox* lb, GtkListBoxRow* row, gpointer user_data);

static void on_widget_rows_changed(GtkListBox* lb, gpointer ud) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(lb);
    on_widget_sel(lb, row, ud);
}

static void on_group_rows_changed(GtkListBox* lb, gpointer ud) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(lb);
    on_group_sel(lb, row, ud);
}

static void on_widget_sel(GtkListBox*, GtkListBoxRow* row, gpointer user_data) {
    auto* app = NA(user_data);
    const int prevSel = app->selWidget;
    if (prevSel >= 0 && prevSel < static_cast<int>(app->rows.size())) ReadEditorToWidget(app, prevSel);

    if (!row) {
        app->selWidget = -1;
        WriteWidgetToEditor(app, -1);
        return;
    }
    app->selWidget = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "nx-idx"));
    WriteWidgetToEditor(app, app->selWidget);
    if (app->selWidget >= 0 && static_cast<size_t>(app->selWidget) < app->rows.size()) {
        if (app->rows[static_cast<size_t>(app->selWidget)].floater)
            gtk_widget_queue_draw(app->rows[static_cast<size_t>(app->selWidget)].floater);
        ApplyWidgetTopmost(app, static_cast<size_t>(app->selWidget));
    }
}

static void on_group_sel(GtkListBox*, GtkListBoxRow* row, gpointer user_data) {
    auto* app = NA(user_data);
    const int prev = app->selGroup;
    if (prev >= 0 && prev < static_cast<int>(app->groups.size())) ReadEditorToGroup(app, prev);

    if (!row) {
        app->selGroup = -1;
        WriteGroupToEditor(app, -1);
        return;
    }
    app->selGroup = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "nx-idx"));
    WriteGroupToEditor(app, app->selGroup);
    if (app->selGroup >= 0 && static_cast<size_t>(app->selGroup) < app->groups.size()) {
        if (app->groups[static_cast<size_t>(app->selGroup)].floater)
            gtk_widget_queue_draw(app->groups[static_cast<size_t>(app->selGroup)].floater);
        ApplyGroupTopmost(app, static_cast<size_t>(app->selGroup));
    }
}

static void on_chk_nonpinned(GtkToggleButton*, gpointer user_data) {
    auto* app = NA(user_data);
    SaveCurrentEditorWidget(app);
    app->showNonPinned = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chkShowNonPinned)) != FALSE;
    SyncFloaterVisibility(app, true);
    LayoutFloaters(app);
    RefreshLists(app);
}

static void on_combo_group(GtkComboBox*, gpointer user_data) {
    auto* app = NA(user_data);
    SaveCurrentEditorWidget(app);
    SyncFloaterVisibility(app, true);
    LayoutFloaters(app);
    RefreshLists(app);
}

static void on_chk_pin(GtkToggleButton*, gpointer user_data) {
    auto* app = NA(user_data);
    ReadEditorToWidget(app, app->selWidget);
    if (app->selWidget >= 0) {
        ApplyWidgetTopmost(app, static_cast<size_t>(app->selWidget));
        SyncFloaterVisibility(app, true);
        LayoutFloaters(app);
    }
    RefreshLists(app);
}

static void on_chk_gpin(GtkToggleButton*, gpointer user_data) {
    auto* app = NA(user_data);
    ReadEditorToGroup(app, app->selGroup);
    if (app->selGroup >= 0) ApplyGroupTopmost(app, static_cast<size_t>(app->selGroup));
    RefreshLists(app);
}

static void btn_run(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    SaveCurrentEditorWidget(app);
    RefreshLists(app);
    RunWidget(app, app->selWidget);
}

static void btn_add(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    SaveCurrentEditorWidget(app);
    Widget w;
    w.name = "New Widget";
    w.alwaysVisible = false;
    w.commands.push_back("echo hello");
    app->rows.push_back({ w, nullptr });
    app->selWidget = static_cast<int>(app->rows.size()) - 1;
    RebuildFloaters(app);
    RefreshLists(app);
    WriteWidgetToEditor(app, app->selWidget);
    SetStatus(app, "Widget added.");
}

static void btn_del(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    if (app->selWidget >= 0 && app->selWidget < static_cast<int>(app->rows.size())) {
        app->rows.erase(app->rows.begin() + app->selWidget);
        app->selWidget = -1;
        RebuildFloaters(app);
        RefreshLists(app);
        SetStatus(app, "Widget deleted.");
    }
}

static void btn_save(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    SaveCurrentEditorWidget(app);
    if (app->selGroup >= 0 && app->selGroup < static_cast<int>(app->groups.size())) ReadEditorToGroup(app, app->selGroup);
    std::vector<Widget> wv;
    std::vector<Group> gv;
    wv.reserve(app->rows.size());
    for (const auto& r : app->rows) wv.push_back(r.w);
    gv.reserve(app->groups.size());
    for (const auto& r : app->groups) gv.push_back(r.g);
    if (SaveWidgets(app->configPath, wv, gv)) SetStatus(app, "Saved to widgets.txt");
    else SetStatus(app, "Failed to save widgets.txt");
    RefreshLists(app);
}

#ifdef HAVE_AYATANA_APPINDICATOR
static void tray_show(GtkMenuItem*, gpointer user_data) {
    auto* app = NA(user_data);
    gtk_widget_show_all(app->window);
    gtk_window_present(GTK_WINDOW(app->window));
}

static void tray_quit(GtkMenuItem*, gpointer user_data) {
    auto* app = NA(user_data);
    g_application_quit(G_APPLICATION(app->gtkApp));
}
#endif

static void btn_hide(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    gtk_widget_hide(app->window);
}

static void btn_add_group(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    if (app->selGroup >= 0 && app->selGroup < static_cast<int>(app->groups.size())) ReadEditorToGroup(app, app->selGroup);
    Group g;
    g.name = Trim(gtk_entry_get_text(GTK_ENTRY(app->entryGroupName)));
    if (g.name.empty()) g.name = std::string("Group ") + std::to_string(app->groups.size() + 1);
    if (FindGroupIndex(app, g.name) >= 0) {
        SetStatus(app, "Group name already exists.");
        return;
    }
    g.alwaysVisible = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->checkGroupPin)) != FALSE;
    g.visible = true;
    app->groups.push_back({ g, nullptr });
    app->selGroup = static_cast<int>(app->groups.size()) - 1;
    RebuildFloaters(app);
    RefreshLists(app);
    WriteGroupToEditor(app, app->selGroup);
    SetStatus(app, "Group added.");
}

static void btn_del_group(GtkButton*, gpointer user_data) {
    auto* app = NA(user_data);
    if (app->selGroup >= 0 && app->selGroup < static_cast<int>(app->groups.size())) {
        const std::string name = app->groups[static_cast<size_t>(app->selGroup)].g.name;
        if (app->groups[static_cast<size_t>(app->selGroup)].floater)
            gtk_widget_destroy(app->groups[static_cast<size_t>(app->selGroup)].floater);
        app->groups.erase(app->groups.begin() + app->selGroup);
        RemoveGroupAssignment(app, name);
        app->selGroup = -1;
        RebuildFloaters(app);
        RefreshLists(app);
        WriteGroupToEditor(app, -1);
        if (app->selWidget >= 0) WriteWidgetToEditor(app, app->selWidget);
        SetStatus(app, "Group deleted.");
    }
}

static GtkWidget* build_ui(NexusApp* app) {
    ApplyDarkCss();

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='xx-large' weight='bold' foreground='#00DCFF'>WIDGET NEXUS</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 4, 1);

    app->chkShowNonPinned = gtk_check_button_new_with_label("Show non-pinned widget windows");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chkShowNonPinned), TRUE);
    g_signal_connect(app->chkShowNonPinned, "toggled", G_CALLBACK(on_chk_nonpinned), app);

    GtkWidget* scrollW = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scrollW, 280, 260);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollW), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    app->listWidgets = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrollW), app->listWidgets);
    g_signal_connect(app->listWidgets, "selected-rows-changed", G_CALLBACK(on_widget_rows_changed), app);

    GtkWidget* btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* bRun = gtk_button_new_with_label("Run");
    GtkWidget* bAdd = gtk_button_new_with_label("Add");
    GtkWidget* bDel = gtk_button_new_with_label("Delete");
    GtkWidget* bSave = gtk_button_new_with_label("Save");
    GtkWidget* bHide = gtk_button_new_with_label("Hide");
    g_signal_connect(bRun, "clicked", G_CALLBACK(btn_run), app);
    g_signal_connect(bAdd, "clicked", G_CALLBACK(btn_add), app);
    g_signal_connect(bDel, "clicked", G_CALLBACK(btn_del), app);
    g_signal_connect(bSave, "clicked", G_CALLBACK(btn_save), app);
    g_signal_connect(bHide, "clicked", G_CALLBACK(btn_hide), app);
    for (GtkWidget* b : { bRun, bAdd, bDel, bSave, bHide }) gtk_box_pack_start(GTK_BOX(btnRow), b, FALSE, FALSE, 0);

    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(left), app->chkShowNonPinned, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), scrollW, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left), btnRow, FALSE, FALSE, 0);

    GtkWidget* lblName = gtk_label_new("Name:");
    gtk_widget_set_halign(lblName, GTK_ALIGN_START);
    app->entryName = gtk_entry_new();

    GtkWidget* lblGrp = gtk_label_new("Group:");
    gtk_widget_set_halign(lblGrp, GTK_ALIGN_START);
    app->comboGroup = gtk_combo_box_text_new();
    g_signal_connect(app->comboGroup, "changed", G_CALLBACK(on_combo_group), app);

    app->checkPinned = gtk_check_button_new_with_label("Always visible (pinned)");
    g_signal_connect(app->checkPinned, "toggled", G_CALLBACK(on_chk_pin), app);

    GtkWidget* lblCmd = gtk_label_new("Commands (one per line):");
    gtk_widget_set_halign(lblCmd, GTK_ALIGN_START);
    app->tvCommands = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->tvCommands), TRUE);
    app->bufCommands = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->tvCommands));
    GtkWidget* scrollT = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scrollT, 360, 160);
    gtk_container_add(GTK_CONTAINER(scrollT), app->tvCommands);

    GtkWidget* lblGL = gtk_label_new("Groups:");
    gtk_widget_set_halign(lblGL, GTK_ALIGN_START);
    GtkWidget* scrollG = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scrollG, 160, 140);
    app->listGroups = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrollG), app->listGroups);
    g_signal_connect(app->listGroups, "selected-rows-changed", G_CALLBACK(on_group_rows_changed), app);

    app->entryGroupName = gtk_entry_new();
    app->checkGroupPin = gtk_check_button_new_with_label("Pin");
    g_signal_connect(app->checkGroupPin, "toggled", G_CALLBACK(on_chk_gpin), app);
    GtkWidget* gBtnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* bAddG = gtk_button_new_with_label("Add G");
    GtkWidget* bDelG = gtk_button_new_with_label("Del G");
    g_signal_connect(bAddG, "clicked", G_CALLBACK(btn_add_group), app);
    g_signal_connect(bDelG, "clicked", G_CALLBACK(btn_del_group), app);
    gtk_box_pack_start(GTK_BOX(gBtnRow), bAddG, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gBtnRow), bDelG, FALSE, FALSE, 0);

    GtkWidget* right = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(right), 6);
    gtk_grid_set_column_spacing(GTK_GRID(right), 8);
    int r = 0;
    gtk_grid_attach(GTK_GRID(right), lblName, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(right), app->entryName, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), lblGrp, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(right), app->comboGroup, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), app->checkPinned, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), lblCmd, 0, r, 3, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), scrollT, 0, r, 3, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), lblGL, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(right), scrollG, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), app->entryGroupName, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), app->checkGroupPin, 1, r, 2, 1);
    ++r;
    gtk_grid_attach(GTK_GRID(right), gBtnRow, 1, r, 2, 1);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(paned), left, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 320);

    gtk_grid_attach(GTK_GRID(grid), paned, 0, 1, 4, 1);

    app->lblStatus = gtk_label_new("Ready.");
    gtk_widget_set_name(app->lblStatus, "status");
    gtk_widget_set_halign(app->lblStatus, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), app->lblStatus, 0, 2, 4, 1);

    return grid;
}

static void on_main_window_destroy(GtkWidget*, gpointer a) {
    DestroyAllFloaters(NA(a));
}

static void activate(GtkApplication* gtkApp, gpointer userData) {
    auto* app = NA(userData);
    app->gtkApp = gtkApp;
    app->configPath = ConfigPathBesideExecutable();

    std::vector<Widget> wv;
    std::vector<Group> gv;
    const bool loadedOk = LoadWidgets(app->configPath, wv, gv);
    app->rows.clear();
    app->groups.clear();
    for (auto& w : wv) app->rows.push_back({ std::move(w), nullptr });
    for (auto& g : gv) app->groups.push_back({ std::move(g), nullptr });

    app->window = gtk_application_window_new(gtkApp);
    gtk_window_set_title(GTK_WINDOW(app->window), "WIDGET NEXUS");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 960, 700);
    gtk_container_add(GTK_CONTAINER(app->window), build_ui(app));

    g_signal_connect(gtk_widget_get_screen(app->window), "monitors-changed", G_CALLBACK(on_display_changed), app);

#ifdef HAVE_AYATANA_APPINDICATOR
    app->indicator = app_indicator_new("widget-nexus-lnx", "utilities-terminal", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(app->indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(app->indicator, "Widget Nexus");
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* miShow = gtk_menu_item_new_with_label("Show Nexus");
    GtkWidget* miQuit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(miShow, "activate", G_CALLBACK(tray_show), app);
    g_signal_connect(miQuit, "activate", G_CALLBACK(tray_quit), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), miShow);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), miQuit);
    gtk_widget_show_all(menu);
    app_indicator_set_menu(app->indicator, GTK_MENU(menu));
#endif

    gtk_widget_show_all(app->window);

    RebuildFloaters(app);
    RefreshLists(app);
    SetStatus(app, loadedOk ? "Loaded widgets.txt" : "Using defaults (widgets.txt missing).");

    g_signal_connect(app->window, "destroy", G_CALLBACK(on_main_window_destroy), app);
}

} // namespace

int main(int argc, char** argv) {
    NexusApp app{};
    GtkApplication* gtkApp = gtk_application_new("com.widgetnexus.lnx", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtkApp, "activate", G_CALLBACK(activate), &app);
    const int status = g_application_run(G_APPLICATION(gtkApp), argc, argv);
    g_object_unref(gtkApp);
    return status;
}

