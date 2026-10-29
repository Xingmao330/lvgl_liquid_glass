#ifndef LIQUID_GLASS_SHADER_H
#define LIQUID_GLASS_SHADER_H

#include "liquid_glass.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t program;
  int32_t backdrop;
  int32_t screen_size;
  int32_t origin;
  int32_t view_scale;
  int32_t size;
  int32_t radius;
  int32_t refraction_height;
  int32_t refraction_amount;
  int32_t dispersion;
  int32_t depth_effect;
  int32_t tint;
  int32_t opacity;
} liquid_glass_shader_t;

/*
 * The shader backend is intentionally separate from the CPU snapshot path.
 * Refraction samples a FULL SCREEN snapshot texture (so the edge band can
 * pull content from beyond the widget, like the Android reference) and
 * writes the refracted widget-sized region to the destination buffer.
 */
bool liquid_glass_shader_available(void);
bool liquid_glass_shader_init(liquid_glass_shader_t * shader);
void liquid_glass_shader_deinit(liquid_glass_shader_t * shader);

/* Convert (BGRA->RGBA) and upload one full screen snapshot. Call once per
 * refresh pass; subsequent process_argb8888 calls sample this texture
 * without re-uploading. */
bool liquid_glass_shader_upload_screen(const liquid_glass_shader_t * shader,
                                      const uint8_t * screen,
                                      uint32_t screen_stride,
                                      int32_t screen_w,
                                      int32_t screen_h);

bool liquid_glass_shader_process_argb8888(const liquid_glass_shader_t * shader,
                                         const uint8_t * screen,
                                         uint32_t screen_stride,
                                         int32_t screen_w,
                                         int32_t screen_h,
                                         int32_t origin_x,
                                         int32_t origin_y,
                                         int32_t view_scale_x,
                                         int32_t view_scale_y,
                                         uint8_t * destination,
                                         uint32_t dst_stride,
                                         int32_t width,
                                         int32_t height,
                                         const liquid_glass_params_t * params);

bool liquid_glass_shader_draw(const liquid_glass_shader_t * shader,
                             uint32_t backdrop_texture,
                             int32_t x,
                             int32_t y,
                             int32_t width,
                             int32_t height,
                             int32_t screen_width,
                             int32_t screen_height,
                             const liquid_glass_params_t * params,
                             float tint_r,
                             float tint_g,
                             float tint_b,
                             float opacity);

#endif
