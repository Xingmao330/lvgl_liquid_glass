#include "liquid_glass.h"

#include <math.h>
#include <string.h>

void liquid_glass_params_init(liquid_glass_params_t * params)
{
  if(params == NULL) return;

  params->refraction_height = 24;
  params->refraction_amount = 18.0f;
  params->dispersion = 0.35f;
  params->vibrancy = 0.06f;
  params->highlight = 0.12f;
  params->corner_radius = 28;
  params->depth_effect = true;
}

static const uint8_t * sample_pixel(const uint8_t * source,
                                   uint32_t stride,
                                   int32_t width,
                                   int32_t height,
                                   float x,
                                   float y)
{
  int32_t ix = (int32_t)(x + 0.5f);
  int32_t iy = (int32_t)(y + 0.5f);

  if(ix < 0) ix = 0;
  if(iy < 0) iy = 0;
  if(ix >= width) ix = width - 1;
  if(iy >= height) iy = height - 1;

  return source + (size_t)iy * stride + (size_t)ix * 4U;
}

static float rounded_rect_distance(float x,
                                   float y,
                                   float half_width,
                                   float half_height,
                                   float radius)
{
  float qx = fabsf(x) - (half_width - radius);
  float qy = fabsf(y) - (half_height - radius);
  float outside_x = fmaxf(qx, 0.0f);
  float outside_y = fmaxf(qy, 0.0f);
  float outside = sqrtf(outside_x * outside_x + outside_y * outside_y) - radius;
  float inside = fminf(fmaxf(qx, qy), 0.0f);

  return outside + inside;
}

static void rounded_rect_normal(float x,
                                float y,
                                float half_width,
                                float half_height,
                                float radius,
                                float * nx,
                                float * ny)
{
  float cx = copysignf(fmaxf(fabsf(x) - half_width + radius, 0.0f), x);
  float cy = copysignf(fmaxf(fabsf(y) - half_height + radius, 0.0f), y);
  float length = sqrtf(cx * cx + cy * cy);

  if(length > 0.001f) {
    *nx = cx / length;
    *ny = cy / length;
  }
  else {
    float corner_x = fabsf(x) - (half_width - radius);
    float corner_y = fabsf(y) - (half_height - radius);
    if(corner_x >= corner_y) {
      *nx = x < 0.0f ? -1.0f : 1.0f;
      *ny = 0.0f;
    }
    else {
      *nx = 0.0f;
      *ny = y < 0.0f ? -1.0f : 1.0f;
    }
  }
}

/*
 * Refraction matches the Android reference (AndroidLiquidGlass AGSL):
 * d = circleMap(1 - -sd/height) * amount, sampled at coord + d * grad -
 * i.e. the edge band samples inward, matching the negative amount used by
 * AndroidLiquidGlass. The interior (beyond the band) passes through unchanged.
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
                                   const liquid_glass_params_t * params)
{
  float half_width = (float)width * 0.5f;
  float half_height = (float)height * 0.5f;
  float radius = fminf((float)params->corner_radius,
                       fminf(half_width, half_height));
  float gradient_radius = fminf(radius * 1.5f,
                               fminf(half_width, half_height));
  float refraction_height = fmaxf((float)params->refraction_height, 1.0f);
  float scale_x = (float)view_scale_x / (float)LV_SCALE_NONE;
  float scale_y = (float)view_scale_y / (float)LV_SCALE_NONE;
  float center_x = origin_x + half_width;
  float center_y = origin_y + half_height;

  for(int32_t y = 0; y < height; y++) {
    for(int32_t x = 0; x < width; x++) {
      size_t dst_offset = (size_t)y * dst_stride + (size_t)x * 4U;
      uint8_t * out = destination + dst_offset;
      float px = (float)x + 0.5f - half_width;
      float py = (float)y + 0.5f - half_height;
      float distance = rounded_rect_distance(px, py, half_width, half_height, radius);

      /* Compensated backdrop position: the widget renders the canvas
       * scaled up by 1/view scale, so the canvas holds the backdrop
       * shrunk by view scale - keeping it pinned on screen. */
      float base_x = center_x + ((float)x - half_width) * scale_x;
      float base_y = center_y + ((float)y - half_height) * scale_y;

      if(distance >= 0.0f || -distance >= refraction_height) {
        /* Interior: pass the compensated backdrop through (clamped at the
         * snapshot bounds). */
        const uint8_t * base = sample_pixel(source, src_stride, src_w, src_h,
                                            base_x, base_y);
        memcpy(out, base, 4U);
        continue;
      }

      float depth = -distance / refraction_height;
      float curve = 1.0f - sqrtf(fmaxf(0.0f, 1.0f - (1.0f - depth) * (1.0f - depth)));
      float nx;
      float ny;
      rounded_rect_normal(
        px,
        py,
        half_width,
        half_height,
        gradient_radius,
        &nx,
        &ny
      );
      if(params->depth_effect) {
        float radial_length = sqrtf(px * px + py * py);

        if(radial_length > 0.001f) {
          nx += px / radial_length;
          ny += py / radial_length;
          float normal_length = sqrtf(nx * nx + ny * ny);
          nx /= normal_length;
          ny /= normal_length;
        }
      }

      /* Android supplies a negative refraction amount. Sample inward from
       * the rounded edge so the backdrop is pulled into the lens. */
      /* Keep the reusable widget as strong as the previous CPU demo. */
      float bend = -curve * params->refraction_amount * 1.35f;
      float dispersion_scale = fminf(params->dispersion, 3.0f) *
                               (px * py) / (half_width * half_height);
      float refracted_x = base_x + bend * nx * scale_x;
      float refracted_y = base_y + bend * ny * scale_y;
      float dispersed_x = bend * nx * dispersion_scale * scale_x;
      float dispersed_y = bend * ny * dispersion_scale * scale_y;

      const uint8_t * red_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x + dispersed_x,
        refracted_y + dispersed_y
      );
      const uint8_t * orange_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x + dispersed_x * (2.0f / 3.0f),
        refracted_y + dispersed_y * (2.0f / 3.0f)
      );
      const uint8_t * yellow_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x + dispersed_x / 3.0f,
        refracted_y + dispersed_y / 3.0f
      );
      const uint8_t * green_sample = sample_pixel(
        source, src_stride, src_w, src_h, refracted_x, refracted_y
      );
      const uint8_t * cyan_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x - dispersed_x / 3.0f,
        refracted_y - dispersed_y / 3.0f
      );
      const uint8_t * blue_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x - dispersed_x * (2.0f / 3.0f),
        refracted_y - dispersed_y * (2.0f / 3.0f)
      );
      const uint8_t * purple_sample = sample_pixel(
        source, src_stride, src_w, src_h,
        refracted_x - dispersed_x,
        refracted_y - dispersed_y
      );

      /* Match the reference shader's seven spectral taps. */
      out[0] = (uint8_t)(cyan_sample[0] / 3.0f +
                         blue_sample[0] / 3.0f +
                         purple_sample[0] / 3.0f);
      out[1] = (uint8_t)(orange_sample[1] / 7.0f +
                         yellow_sample[1] / 3.5f +
                         green_sample[1] / 3.5f +
                         cyan_sample[1] / 3.5f);
      out[2] = (uint8_t)(red_sample[2] / 3.5f +
                         orange_sample[2] / 3.5f +
                         yellow_sample[2] / 3.5f +
                         purple_sample[2] / 7.0f);

      float luminance = out[2] * 0.2126f +
                        out[1] * 0.7152f +
                        out[0] * 0.0722f;
      float saturation = 1.0f + params->vibrancy;
      float edge_light = fabsf(nx * -0.45f + ny * -0.89f);
      float highlight = powf(edge_light, 8.0f) *
                        curve * params->highlight * 255.0f;

      out[0] = (uint8_t)fminf(
        255.0f,
        fmaxf(0.0f, luminance + (out[0] - luminance) * saturation + highlight)
      );
      out[1] = (uint8_t)fminf(
        255.0f,
        fmaxf(0.0f, luminance + (out[1] - luminance) * saturation + highlight)
      );
      out[2] = (uint8_t)fminf(
        255.0f,
        fmaxf(0.0f, luminance + (out[2] - luminance) * saturation + highlight)
      );
      out[3] = 255;
    }
  }
}
