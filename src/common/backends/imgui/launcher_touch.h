#pragma once
/* Touch-screen helpers for the launcher (Android). Included by
 * launcher_imgui.cpp; call launcher_touch_frame() right after ImGui::NewFrame().
 *
 *  - Drag to scroll: a finger that slides vertically scrolls the list under it
 *    instead of pressing the button it started on.
 *  - Long press: a finger held still shows the control's tooltip (ImGui's own
 *    stationary hover) and the release no longer clicks it.
 *
 * Tapping a text box opens the system keyboard by itself: the SDL3 ImGui
 * backend calls SDL_StartTextInput() whenever an InputText wants text. The
 * keyboard covers the lower half of a landscape screen and the window is not
 * resized under it, so while it is up the whole UI is drawn shifted upward
 * enough to keep the text box in view (launcher_touch_ime), and touches are
 * shifted to match (launcher_touch_shift_event). */
#if defined(__ANDROID__)
#include "imgui.h"
#include "imgui_internal.h"
#include <math.h>

static void launcher_touch_frame(float unit) {
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    static bool s_scrolling = false, s_long_press = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        s_scrolling = s_long_press = false;
        return;
    }
    if (io.MouseSource != ImGuiMouseSource_TouchScreen) return;
    const ImVec2 from = io.MouseClickedPos[0];
    const float dx = io.MousePos.x - from.x, dy = io.MousePos.y - from.y;
    const float slop = 12.0f * unit;

    /* The nearest window under the finger that can scroll vertically. */
    ImGuiWindow* w = g.HoveredWindow;
    while (w && w->ScrollMax.y <= 0.0f && (w->Flags & ImGuiWindowFlags_ChildWindow))
        w = w->ParentWindow;
    const bool can_scroll = w && w->ScrollMax.y > 0.0f;

    if (!s_scrolling && !s_long_press) {
        /* Mostly vertical, past the slop: a scroll, not a press. Horizontal
         * drags stay with the control (sliders). */
        if (can_scroll && fabsf(dy) > slop && fabsf(dy) > 2.0f * fabsf(dx)) {
            s_scrolling = true;
            ImGui::ClearActiveID();   /* the button it started on is not pressed */
        } else if (io.MouseDownDuration[0] > 0.6f && fabsf(dx) < slop && fabsf(dy) < slop &&
                   g.ActiveId != 0 && !io.WantTextInput) {
            /* Held still: the tooltip shows; letting go does not click. */
            s_long_press = true;
            ImGui::ClearActiveID();
        }
    }
    if (s_scrolling && can_scroll)
        ImGui::SetScrollY(w, w->Scroll.y - io.MouseDelta.y);
}

static float s_ime_shift;   /* logical units the UI is drawn raised by */

/* After ImGui::Render(): raise the frame when the text box being typed into
 * would sit under the on-screen keyboard. */
static void launcher_touch_ime(ImDrawData* dd, float display_h) {
    ImGuiContext& g = *GImGui;
    float shift = 0.0f;
    SDL_Window* w = SDL_GetKeyboardFocus();
    if (g.PlatformImeData.WantVisible && w && SDL_ScreenKeyboardShown(w)) {
        const float bottom = g.PlatformImeData.InputPos.y + g.PlatformImeData.InputLineHeight;
        const float target = display_h * 0.32f;   /* above a landscape keyboard */
        if (bottom > target) shift = bottom - target;
    }
    s_ime_shift = shift;
    if (dd) dd->DisplayPos.y += shift;
}

/* Pointer events land on the raised frame: move them into layout space. */
static void launcher_touch_shift_event(SDL_Event& e) {
    if (s_ime_shift == 0.0f) return;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: e.motion.y += s_ime_shift; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: e.button.y += s_ime_shift; break;
    case SDL_EVENT_MOUSE_WHEEL: e.wheel.mouse_y += s_ime_shift; break;
    default: break;
    }
}
#else
static inline void launcher_touch_frame(float) {}
static inline void launcher_touch_ime(ImDrawData*, float) {}
template <typename E> static inline void launcher_touch_shift_event(E&) {}
#endif
