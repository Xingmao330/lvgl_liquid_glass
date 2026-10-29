#ifndef LIQUID_GLASS_BUTTON_H
#define LIQUID_GLASS_BUTTON_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a reusable liquid glass button.
 *
 * The control owns its glass surface and implements the WebGL reference
 * interaction: a springy press, finger-following translation, directional
 * squash/stretch, a moving press highlight, and a spring release.
 */
lv_obj_t * liquid_glass_button_create(lv_obj_t * parent,
                                      const char * text,
                                      int32_t width,
                                      int32_t height);

void liquid_glass_button_set_text(lv_obj_t * button, const char * text);

/** Set the button's liquid-glass tint while keeping it on button updates. */
void liquid_glass_button_set_tint(lv_obj_t * button,
                                  lv_color_t color,
                                  lv_opa_t opa);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
