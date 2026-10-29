#ifndef LIQUID_GLASS_WIDGET_H
#define LIQUID_GLASS_WIDGET_H

#include "liquid_glass.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reusable liquid glass layer for LVGL.
 *
 * liquid_glass_apply() turns any widget (panel, button, ...) into a liquid
 * glass surface: an internal canvas snapshot of whatever is behind the widget
 * is blurred, refracted and optionally tinted, then shown as the widget's
 * background image. Content added as children of the widget renders on top
 * of the glass as usual.
 *
 * A global manager keeps every registered surface up to date, so glass
 * widgets can overlap each other, be dragged around, or sit on top of live
 * content without any manual refresh.
 */

typedef enum {
    /** Re-render every surface on every manager tick (default). This is the
     *  "real time" mode: overlapping glass, drags and live backdrops update
     *  by themselves. */
    LIQUID_GLASS_REFRESH_ALWAYS = 0,
    /** Re-render only surfaces marked dirty (moves, style/param changes, or
     *  explicit liquid_glass_mark_dirty() calls). Cheaper, but the
     *  application has to mark what changed. */
    LIQUID_GLASS_REFRESH_DIRTY,
    /** No manager timer at all; call liquid_glass_refresh[_all]() yourself. */
    LIQUID_GLASS_REFRESH_MANUAL,
} liquid_glass_refresh_policy_t;

/** Turn any widget into a liquid glass surface.
 *  @param obj     any widget; it must outlive the glass effect (call
 *                 liquid_glass_remove() first if you plan to reparent it)
 *  @param params  refraction parameters, or NULL for defaults
 *  @return        true on success */
bool liquid_glass_apply(lv_obj_t * obj, const liquid_glass_params_t * params);

/** Detach the glass effect from a widget and release its buffers. */
bool liquid_glass_remove(lv_obj_t * obj);

/** Check whether a widget currently has the glass effect applied. */
bool liquid_glass_is_applied(const lv_obj_t * obj);

/** Convenience: create a plain container that is already liquid glass. */
lv_obj_t * liquid_glass_create(lv_obj_t * parent, int32_t width, int32_t height);

/** Refraction parameters of a glass widget (NULL if not applied). Edit the
 *  returned struct and call liquid_glass_refresh() to apply immediately, or
 *  wait for the next manager tick. */
liquid_glass_params_t * liquid_glass_get_params(lv_obj_t * obj);

/** Background blur radius in pixels (default 2, 0 disables the blur). */
void liquid_glass_set_blur(lv_obj_t * obj, int32_t blur_radius);

/** Flat color layer drawn over the refracted content. */
void liquid_glass_set_tint(lv_obj_t * obj, lv_color_t color, lv_opa_t opa);

/** Re-render one glass surface now, and mark overlapping surfaces dirty so
 *  they pick up the change on the next manager tick. */
void liquid_glass_refresh(lv_obj_t * obj);

/** When a surface is refreshed, the snapshot must not contain the surface
 *  itself. By default only the glass widget is hidden for the snapshot; if
 *  sibling overlays are drawn on top of the glass (e.g. a separate content
 *  layer that must stay crisp while the glass deforms), point the hide
 *  target at their common parent so they disappear together. */
void liquid_glass_set_hide_target(lv_obj_t * glass, lv_obj_t * target);

/** Sample the currently rendered backdrop below this surface on each refresh.
 *  This opt-in mode snapshots the parent object tree and is useful when the
 *  glass must distort crisp widgets layered over a lower glass surface. */
void liquid_glass_set_live_backdrop(lv_obj_t * obj, bool enabled);

/** Re-render every glass surface on the active screen now, back to front. */
void liquid_glass_refresh_all(void);

/** Mark one surface dirty (only relevant for LIQUID_GLASS_REFRESH_DIRTY). */
void liquid_glass_mark_dirty(lv_obj_t * obj);

/** Compensate a transform applied to the widget: while the widget is
 *  rendered scaled (press animations), the backdrop captured in the glass
 *  would zoom along with it, revealing that it is a frozen snapshot. With
 *  a view scale set, the backdrop is pre-zoomed by the inverse factor when
 *  the surface is rendered, so the background stays visually pinned while
 *  the glass edges and children deform. Scale units are LVGL transform
 *  units (LV_SCALE_NONE == 256 == 100%). Pass LV_SCALE_NONE to disable.
 *
 *  The compensation assumes the widget scales around its center; set the
 *  transform pivot styles to w/2 and h/2 (the style default is the
 *  top-left corner). See src/glass_pinning_test.c for a headless check. */
void liquid_glass_set_view_scale_xy(lv_obj_t * obj, int32_t scale_x,
                                    int32_t scale_y);

/** Master switch for the glass rendering. When disabled, surfaces skip
 *  blur and refraction and only keep their tint, so glass becomes clear
 *  plastic (and stops spending CPU on the effect). Marks all dirty. */
void liquid_glass_set_enabled(bool enabled);
bool liquid_glass_is_enabled(void);

/** Global parameter overrides. When a global value is set (>= 0) it takes
 *  precedence over every surface's own parameter - this is what a settings
 *  app binds its controls to. Pass -1 to fall back to the per-surface
 *  value. float parameters are transported as x10 integers. Changing a
 *  global marks all surfaces dirty. */
void liquid_glass_set_global_blur(int32_t blur_radius);
void liquid_glass_set_global_refraction(int32_t height, int32_t amount_x10);
void liquid_glass_set_global_dispersion(int32_t dispersion_x10);
void liquid_glass_set_global_tint_opa(int32_t opa);

int32_t liquid_glass_get_global_blur(void);
int32_t liquid_glass_get_global_refraction_height(void);
int32_t liquid_glass_get_global_refraction_amount_x10(void);
int32_t liquid_glass_get_global_dispersion_x10(void);
int32_t liquid_glass_get_global_tint_opa(void);

/** Debug/test helper: the canvas draw buffer holding the glass content of a
 *  surface (NULL if not applied or not sized yet). */
const lv_draw_buf_t * liquid_glass_debug_canvas_buf(const lv_obj_t * obj);

/** Manager refresh policy (default LIQUID_GLASS_REFRESH_ALWAYS). */
void liquid_glass_set_refresh_policy(liquid_glass_refresh_policy_t policy);
liquid_glass_refresh_policy_t liquid_glass_get_refresh_policy(void);

/** Manager tick period in ms (default 33). */
void liquid_glass_set_refresh_period(uint32_t period_ms);

/** Choose the refraction backend. CPU is the default; SHADER/AUTO try to
 *  bring up the desktop OpenGL path once and silently fall back to the CPU
 *  if it is unavailable. */
void liquid_glass_set_render_mode(liquid_glass_render_mode_t mode);
liquid_glass_render_mode_t liquid_glass_get_render_mode(void);
bool liquid_glass_using_shader(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
