# Widget Nexus Style Spec v2

This document defines a concrete visual system for the Win32/GDI implementation in `main.cpp`.

## Goals

- Keep the neon identity, but reduce eye fatigue.
- Improve hierarchy and readability in dense editor screens.
- Preserve performance and implementation simplicity in GDI.
- Stay compatible with current owner-draw controls and custom paint pipeline.

## Design Principles

- Use glow sparingly: focus/selection moments only.
- Prefer contrast and spacing over heavy strokes.
- Keep one accent dominant (cyan), one accent secondary (magenta).
- Make state colors semantic (success/warn/error/info) and reusable.

## Token Set

### Color Tokens

All values are RGB and intended to map to `COLORREF`.

- `bg.canvas` = `RGB(8, 10, 20)`  
  Main application backdrop.
- `bg.panel` = `RGB(16, 20, 42)`  
  Left/right panel wells.
- `bg.control` = `RGB(12, 16, 34)`  
  Inputs/list surfaces.
- `bg.controlHover` = `RGB(18, 24, 48)`  
  Hover/highlight state for controls.
- `bg.controlActive` = `RGB(24, 34, 66)`  
  Active/selected state.
- `bg.status` = `RGB(10, 14, 30)`  
  Status bar strip.

- `text.primary` = `RGB(224, 246, 255)`  
  Main labels/content.
- `text.secondary` = `RGB(154, 188, 208)`  
  Secondary labels.
- `text.muted` = `RGB(112, 142, 166)`  
  Low emphasis.
- `text.onAccent` = `RGB(6, 12, 18)`  
  Text over strong accent fills.

- `accent.primary` = `RGB(0, 220, 235)`  
  Default emphasis/focus.
- `accent.primaryDim` = `RGB(0, 128, 158)`  
  Subtle border/guide.
- `accent.secondary` = `RGB(235, 72, 198)`  
  Rare highlight (title underline, selected edge).

- `state.success` = `RGB(58, 188, 112)`
- `state.warn` = `RGB(236, 176, 64)`
- `state.error` = `RGB(228, 84, 96)`
- `state.info` = `RGB(76, 168, 228)`

### Stroke and Glow Tokens

- `stroke.outer` = `1px`, color `accent.primaryDim`
- `stroke.inner` = `1px`, color `accent.primary` (focus/selected only)
- `stroke.strong` = `2px`, color `accent.primary` (limited to key affordances)
- `glow.soft` = 20-30% alpha equivalent, only for:
  - selected list row edge
  - focused button
  - floater active ring

### Spacing Tokens

Use an 8px rhythm.

- `space.1` = `4`
- `space.2` = `8`
- `space.3` = `12`
- `space.4` = `16`
- `space.5` = `24`
- `space.6` = `32`

Layout guidance:

- Major panel padding: `16`
- Control vertical gap: `8`
- Section gap: `24`
- Inner text inset for list rows/inputs: `10-12`

### Radius Tokens

- `radius.control` = `6` (buttons/inputs conceptual target)
- `radius.panel` = `8` (if rounded rectangles are added)
- Floaters remain circular (`100%`).

For pure rectangular controls where rounding is expensive in current draw path, fake the feel with softer border contrast and internal padding.

### Typography Tokens

Windows font: `Segoe UI` everywhere except command editor (`Consolas`).

- `type.title`: 24px, bold
- `type.section`: 14px, semibold (or bold fallback)
- `type.body`: 14-15px, normal
- `type.caption`: 12px, normal
- `type.mono`: 14px, normal (`Consolas`)

Usage:

- App title: `type.title`
- Field labels and section labels: `type.section`
- Button/list/input text: `type.body`
- Status details/help text: `type.caption`

## Component Specs

### Window Frame and Panels

- Canvas uses `bg.canvas`.
- Left/right wells use `bg.panel`.
- Replace double heavy outer rectangle with:
  - outer 1px `accent.primaryDim`
  - optional inner 1px `accent.primary` only near header/title region
- Keep title underline but reduce saturation: use `accent.secondary` at ~80% intent.

### Buttons (Owner-Draw)

Default:

- Fill: `bg.control`
- Border: `stroke.outer`
- Text: `text.primary`

Hover:

- Fill: `bg.controlHover`
- Border: `accent.primary`

Pressed:

- Fill: interpolate toward `bg.controlActive`
- Border: `accent.primary`
- Optional 1px content shift down-right for tactile press

Focus:

- Add inner focus ring in `accent.primary` (1px) + subtle glow pass

Disabled:

- Fill: darkened `bg.control`
- Text: `text.muted`
- Border: `accent.primaryDim`

### List Rows (Widget List)

Default:

- Fill: `bg.panel`
- Border: none or very subtle separator line
- Text: `text.secondary`

Hover (future if mouse tracking added):

- Fill: `bg.controlHover`
- Text: `text.primary`

Selected:

- Fill: `bg.controlActive`
- Left accent bar (2px) in `accent.primary`
- Text: `text.primary`
- Secondary tag text (e.g. group) in lighter `text.secondary`

Focus:

- Keep standard focus rect for accessibility, but blend with neon edge treatment.

### Inputs and Combo Boxes

- Background: `bg.control`
- Text: `text.primary`
- Border: `stroke.outer`
- Focus border: `accent.primary`
- Caret/selection: default system behavior unless custom paint is added.

### Status Bar

- Reserve a dedicated strip using `bg.status`.
- Prefix status with semantic token:
  - success -> `state.success`
  - error -> `state.error`
  - info -> `state.info`
  - warning -> `state.warn`
- Keep message text in `text.primary`.

Example format:

- `INFO  Loaded widgets from widgets.txt`
- `OK    Saved to widgets.txt`
- `ERR   Failed to save widgets.txt`

### Floaters

- Maintain circular silhouette and current alpha behavior.
- Use primary accent ring by default; secondary accent only on explicit emphasis.
- Group floater ON/OFF fills should move to semantic state tones:
  - ON base near `state.success` family (darkened for neon theme)
  - OFF base near `state.error` family (darkened)
- Keep text always high-contrast (`text.primary`-like target).

## State Matrix

### Interaction States

Every interactive control should map these states consistently:

- `default`
- `hover`
- `pressed`
- `focused`
- `disabled`
- `selected` (when applicable)

Priority order when multiple apply:

1. disabled
2. pressed
3. selected
4. focused
5. hover
6. default

## Accessibility and Readability Targets

- Body text contrast target: at least WCAG-like 4.5:1 intent.
- Do not rely on color alone for status:
  - include prefixes like `OK/ERR/INFO/WARN`
- Preserve keyboard focus visuals for all controls.
- Keep minimum clickable sizes close to current 34px button height or larger.

## Implementation Mapping (Current Code)

### Existing areas to update first

- Palette constants near neon colors (`kRgb*` block).
- GDI brush/pens created in `ThemeCreate()`.
- Main frame and panel paint in `PaintNeonWindow()`.
- Button render in `DrawOwnerDrawButton()`.
- Widget row render in `DrawOwnerDrawListItem()`.
- Status text patterns in `SetStatus()` call sites.

### Suggested rollout

1. Replace color constants with v2 tokens.
2. Soften frame/border treatment in `PaintNeonWindow()`.
3. Update owner-draw button and list state visuals.
4. Add semantic status text prefixes.
5. Tune floater ON/OFF semantic coloring.

## Optional Light Theme Path

Keep token names semantic (`bg.panel`, `text.primary`, `accent.primary`) so a light theme can be added later by swapping values without changing drawing logic.

