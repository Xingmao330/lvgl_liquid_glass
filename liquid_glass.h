#ifndef LIQUID_GLASS_H
#define LIQUID_GLASS_H

#include "lvgl/lvgl.h"

typedef enum {
  LIQUID_GLASS_RENDER_CPU = 0,
  LIQUID_GLASS_RENDER_SHADER,
  LIQUID_GLASS_RENDER_AUTO
} liquid_glass_render_mode_t;

typedef struct {
  int32_t refraction_height; /**< Edge depth affected by refraction, in pixels. */
  float refraction_amount;   /**< Displacement at the edge, in pixels (outward). */
  float dispersion;          /**< RGB channel separation strength. */
  float vibrancy;            /**< Saturation boost applied to refracted color. */
  float highlight;           /**< Reflected edge light strength, from 0 to 1. */
  int32_t corner_radius;     /**< Rounded rectangle radius, in pixels. */
  bool depth_effect;         /**< Add a radial component to the edge normal. */
} liquid_glass_params_t;

void liquid_glass_params_init(liquid_glass_params_t * params);

/*
 * Refract a widget-sized region (width x height at origin within the source
 * image) in software, sampling the FULL source image so the edge band can
 * pull in content from outside the widget, like the Android reference.
 * The interior passes through at 1/view_scale magnification and the edge
 * displacement shrinks by the same factor, which keeps the backdrop pinned
 * while the widget is rendered scaled (press animations; 256 == 100%).
 * source/destination must not overlap. Rows may have independent strides.
 */
void liquid_glass_refract_argb8888(const uint8_t * source,
                                   uint32_t src_stride,
                                   int32_t src_w,
                                   int32_t src_h,
                                   int32_t origin_x,
                                   int32_t origin_y,
                                   uint8_t * destination,
                                   uint32_t dst_stride,
                                   int32_t width,
                                   int32_t height,
                                   int32_t view_scale_x,
                                   int32_t view_scale_y,
                                   const liquid_glass_params_t * params);

#endif
