#ifndef LIQUID_GLASS_INTERACTIVE_H
#define LIQUID_GLASS_INTERACTIVE_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t refraction_height;
  float refraction_amount;
  float dispersion;
  int32_t blur_radius;
  lv_color_t tint_color;
  lv_opa_t tint_opa;
} liquid_glass_interactive_style_t;

/* Create a label-free liquid-glass surface with spring press and drag feedback. */
lv_obj_t * liquid_glass_interactive_create(lv_obj_t * parent,
                                           int32_t width,
                                           int32_t height,
                                           int32_t radius);

void liquid_glass_interactive_set_style(
  lv_obj_t * obj,
  const liquid_glass_interactive_style_t * style);

void liquid_glass_interactive_set_tint(lv_obj_t * obj,
                                       lv_color_t color,
                                       lv_opa_t opa);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
