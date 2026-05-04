#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

void UpdateFloaterIndices();
void UpdateGroupFloaterIndices();
void ApplyFloaterTopmost(size_t i);
void ApplyGroupFloaterTopmost(size_t i);
void LayoutFloatingWidgets();
void LayoutGroupFloaters();
void CreateFloaterForIndex(HINSTANCE inst, size_t i);
void CreateGroupFloaterForIndex(HINSTANCE inst, size_t i);
void DestroyAllFloaters();
void RebuildAllFloaters(HINSTANCE inst);

void PushFloaterFadeIn(HWND hwnd);
void PushFloaterFadeOut(HWND hwnd);
void PushGroupFloaterPulse(HWND hwnd);
void TickFloaterAnimations();
void EnsureFloaterAnimTimer();
void StopFloaterAnimTimerIfIdle();

void EnsureUiAnimTimer();
void StopUiAnimTimerIfIdle();
void StartButtonPressAnim(HWND hwnd);
void StartWidgetSelectionAnim(HWND listHwnd, int selectedItemData);
void TickUiAnimations();

void SyncFloaterVisibility(bool animate = true);
void RefreshFloaterChrome(size_t i);
void RefreshGroupFloaterChrome(size_t i);

RECT GetPrimaryWorkArea();

int FloaterDiameterPx();
int GroupFloaterDiameterPx();
int FloaterDragRingPx();
