#include "liquid_glass_button.h"

#include "liquid_glass_widget.h"

#include <math.h>

#define GLASS_BUTTON_SCALE_RATIO  (2.0f / 48.0f)
#define GLASS_BUTTON_SPRING_OMEGA 17.320508f
#define GLASS_BUTTON_SPRING_DAMP  0.68f
#define GLASS_BUTTON_HIGHLIGHT_R  34

typedef struct {
  lv_obj_t * button;
  lv_obj_t * highlight;
  lv_obj_t * label;
  lv_timer_t * timer;
  lv_point_t start_drag;
  lv_point_t drag;
  float press;
  float press_velocity;
  float target_press;
  float drag_x_velocity;
  float drag_y_velocity;
  lv_color_t tint_color;
  lv_opa_t tint_opa;
  bool pressed;
  uint32_t last_tick;
} liquid_glass_button_state_t;

static float spring_step(float current, float velocity, float target,
                         float dt, float * next_velocity)
{
  float x0 = current - target;
  float omega_d = GLASS_BUTTON_SPRING_OMEGA *
                  sqrtf(1.0f - GLASS_BUTTON_SPRING_DAMP *
                        GLASS_BUTTON_SPRING_DAMP);
  float decay = expf(-GLASS_BUTTON_SPRING_DAMP *
                     GLASS_BUTTON_SPRING_OMEGA * dt);
  float cos_wd = cosf(omega_d * dt);
  float sin_wd = sinf(omega_d * dt);
  float b0 = (velocity + GLASS_BUTTON_SPRING_DAMP *
              GLASS_BUTTON_SPRING_OMEGA * x0) / omega_d;
  float offset = x0 * decay * cos_wd + b0 * decay * sin_wd;

  *next_velocity =
    -GLASS_BUTTON_SPRING_DAMP * GLASS_BUTTON_SPRING_OMEGA * offset +
    decay * (-x0 * omega_d * sin_wd + b0 * omega_d * cos_wd);
  return target + offset;
}

static float clamp_float(float value, float min_value, float max_value)
{
  if(value < min_value) return min_value;
  if(value > max_value) return max_value;
  return value;
}

static void liquid_glass_button_update_visual(
  liquid_glass_button_state_t * state)
{
  int32_t width;
  int32_t height;
  int32_t max_dim;
  int32_t min_dim;
  float dx;
  float dy;
  float angle;
  float scale;
  float scale_x;
  float scale_y;
  float translation_x;
  float translation_y;
  float width_cap;
  float height_cap;
  int32_t scale_x_lv;
  int32_t scale_y_lv;
  lv_opa_t highlight_opa;
  lv_opa_t tint_opa;

  if(state == NULL || state->button == NULL) return;

  width = lv_obj_get_width(state->button);
  height = lv_obj_get_height(state->button);
  max_dim = LV_MAX(width, height);
  min_dim = LV_MIN(width, height);
  if(width <= 0 || height <= 0 || min_dim <= 0) return;

  dx = (float)state->drag.x - (float)state->start_drag.x;
  dy = (float)state->drag.y - (float)state->start_drag.y;
  angle = atan2f(dy, dx);
  scale = 1.0f + GLASS_BUTTON_SCALE_RATIO * state->press;
  width_cap = LV_MIN((float)width / (float)height, 1.0f);
  height_cap = LV_MIN((float)height / (float)width, 1.0f);

  /* Same tanh drag response and directional deformation as the WebGL
   * LiquidButton layer: the finger pulls the body while the leading axis
   * stretches and the perpendicular axis compresses slightly. */
  translation_x = (float)min_dim *
                  tanhf(0.05f * dx / (float)min_dim);
  translation_y = (float)min_dim *
                  tanhf(0.05f * dy / (float)min_dim);
  scale_x = scale + GLASS_BUTTON_SCALE_RATIO *
            fabsf(cosf(angle) * dx / (float)max_dim) * width_cap;
  scale_y = scale + GLASS_BUTTON_SCALE_RATIO *
            fabsf(sinf(angle) * dy / (float)max_dim) * height_cap;

  scale_x_lv = (int32_t)lroundf(256.0f * scale_x);
  scale_y_lv = (int32_t)lroundf(256.0f * scale_y);
  if(scale_x_lv < 1) scale_x_lv = 1;
  if(scale_y_lv < 1) scale_y_lv = 1;

  lv_obj_set_style_transform_scale_x(state->button, scale_x_lv, 0);
  lv_obj_set_style_transform_scale_y(state->button, scale_y_lv, 0);
  lv_obj_set_style_translate_x(state->button,
                               (int32_t)lroundf(translation_x), 0);
  lv_obj_set_style_translate_y(state->button,
                               (int32_t)lroundf(translation_y), 0);
  liquid_glass_set_view_scale_xy(state->button, scale_x_lv, scale_y_lv);
  tint_opa = (lv_opa_t)lroundf(
    clamp_float((float)state->tint_opa +
                20.0f * clamp_float(state->press, 0.0f, 1.0f),
                0.0f, 255.0f));
  liquid_glass_set_tint(state->button, state->tint_color, tint_opa);

  /* The highlight is a soft white finger light. It is a child of the
   * button, so LVGL transforms it together with the refracted surface. */
  if(state->highlight != NULL) {
    int32_t glow_x = state->drag.x - GLASS_BUTTON_HIGHLIGHT_R;
    int32_t glow_y = state->drag.y - GLASS_BUTTON_HIGHLIGHT_R;
    lv_obj_set_pos(state->highlight, glow_x, glow_y);
    highlight_opa = (lv_opa_t)lroundf(
      clamp_float(state->press, 0.0f, 1.0f) * 58.0f);
    lv_obj_set_style_bg_opa(state->highlight, highlight_opa, 0);
    lv_obj_set_style_shadow_opa(state->highlight,
                                (lv_opa_t)(highlight_opa * 0.75f), 0);
  }

  liquid_glass_mark_dirty(state->button);
}

static void liquid_glass_button_timer_cb(lv_timer_t * timer)
{
  liquid_glass_button_state_t * state = lv_timer_get_user_data(timer);
  uint32_t now;
  float dt;
  float next_velocity;
  float dx;
  float dy;
  bool moving;

  if(state == NULL || state->button == NULL) return;

  now = lv_tick_get();
  if(state->last_tick == 0) state->last_tick = now;
  dt = (float)(now - state->last_tick) / 1000.0f;
  state->last_tick = now;
  if(dt <= 0.0f) return;
  if(dt > 0.05f) dt = 0.05f;

  state->press = spring_step(state->press, state->press_velocity,
                             state->target_press, dt, &next_velocity);
  state->press_velocity = next_velocity;

  if(!state->pressed) {
    state->drag.x = (int32_t)lroundf(spring_step(
      (float)state->drag.x, state->drag_x_velocity,
      (float)state->start_drag.x, dt, &state->drag_x_velocity));
    state->drag.y = (int32_t)lroundf(spring_step(
      (float)state->drag.y, state->drag_y_velocity,
      (float)state->start_drag.y, dt, &state->drag_y_velocity));
  }

  liquid_glass_button_update_visual(state);

  dx = (float)state->drag.x - (float)state->start_drag.x;
  dy = (float)state->drag.y - (float)state->start_drag.y;
  moving = fabsf(state->target_press - state->press) > 0.003f ||
           fabsf(state->press_velocity) > 0.003f ||
           fabsf(dx) > 1.0f || fabsf(dy) > 1.0f ||
           fabsf(state->drag_x_velocity) > 1.0f ||
           fabsf(state->drag_y_velocity) > 1.0f;
  if(!moving) {
    state->press = state->target_press;
    state->press_velocity = 0.0f;
    if(!state->pressed) {
      state->drag = state->start_drag;
      state->drag_x_velocity = 0.0f;
      state->drag_y_velocity = 0.0f;
    }
    liquid_glass_button_update_visual(state);
    lv_timer_pause(state->timer);
  }
}

static void liquid_glass_button_start_timer(
  liquid_glass_button_state_t * state)
{
  if(state == NULL) return;
  state->last_tick = lv_tick_get();
  if(state->timer != NULL) {
    lv_timer_resume(state->timer);
    lv_timer_ready(state->timer);
  }
}

static void liquid_glass_button_event_cb(lv_event_t * e)
{
  liquid_glass_button_state_t * state = lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev = lv_event_get_param(e);
  lv_point_t point;
  lv_area_t button_area;

  if(state == NULL || state->button == NULL) return;

  if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(state->button, &button_area);
    point.x -= button_area.x1 -
               lv_obj_get_style_translate_x(state->button, 0);
    point.y -= button_area.y1 -
               lv_obj_get_style_translate_y(state->button, 0);

    if(code == LV_EVENT_PRESSED) {
      state->start_drag = point;
      state->drag = point;
      state->drag_x_velocity = 0.0f;
      state->drag_y_velocity = 0.0f;
      state->pressed = true;
      state->target_press = 1.0f;
    }
    else if(state->pressed) {
      if(state->drag.x != point.x || state->drag.y != point.y) {
        state->drag = point;
        liquid_glass_button_update_visual(state);
        liquid_glass_button_start_timer(state);
      }
    }
    if(code == LV_EVENT_PRESSED) {
      liquid_glass_button_update_visual(state);
      liquid_glass_button_start_timer(state);
    }
  }
  else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    state->pressed = false;
    state->target_press = 0.0f;
    liquid_glass_button_start_timer(state);
  }
}

static void liquid_glass_button_delete_cb(lv_event_t * e)
{
  liquid_glass_button_state_t * state = lv_event_get_user_data(e);

  if(state == NULL) return;
  if(state->timer != NULL) lv_timer_del(state->timer);
  lv_free(state);
}

lv_obj_t * liquid_glass_button_create(lv_obj_t * parent,
                                      const char * text,
                                      int32_t width,
                                      int32_t height)
{
  liquid_glass_button_state_t * state;
  liquid_glass_params_t * params;
  lv_obj_t * button;

  if(parent == NULL || width <= 0 || height <= 0) return NULL;

  button = lv_obj_create(parent);
  state = lv_malloc(sizeof(*state));
  if(state == NULL) return button;
  lv_memzero(state, sizeof(*state));
  state->button = button;
  state->tint_color = lv_color_hex(0xffffff);
  state->tint_opa = 100;
  state->start_drag.x = width / 2;
  state->start_drag.y = height / 2;
  state->drag = state->start_drag;

  lv_obj_set_size(button, width, height);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(button, height / 2, 0);
  lv_obj_set_style_clip_corner(button, true, 0);
  lv_obj_set_style_bg_opa(button, 0, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_transform_pivot_x(button, width / 2, 0);
  lv_obj_set_style_transform_pivot_y(button, height / 2, 0);
  lv_obj_set_style_shadow_color(button, lv_color_hex(0x15385f), 0);
  lv_obj_set_style_shadow_width(button, 14, 0);
  lv_obj_set_style_shadow_offset_y(button, 6, 0);
  lv_obj_set_style_shadow_opa(button, 90, 0);

  liquid_glass_apply(button, NULL);
  params = liquid_glass_get_params(button);
  if(params != NULL) {
    params->refraction_height = 18;
    params->refraction_amount = 12.0f;
    params->dispersion = 0.35f;
    params->corner_radius = height / 2;
    params->highlight = 0.4f;
  }
  liquid_glass_set_blur(button, 3);
  liquid_glass_set_tint(button, lv_color_hex(0xffffff), 100);

  state->highlight = lv_obj_create(button);
  lv_obj_set_size(state->highlight, GLASS_BUTTON_HIGHLIGHT_R * 2,
                  GLASS_BUTTON_HIGHLIGHT_R * 2);
  lv_obj_clear_flag(state->highlight, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(state->highlight, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(state->highlight, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(state->highlight, 0, 0);
  lv_obj_set_style_border_width(state->highlight, 0, 0);
  lv_obj_set_style_shadow_color(state->highlight, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_shadow_width(state->highlight, 18, 0);
  lv_obj_set_style_shadow_opa(state->highlight, 0, 0);

  state->label = lv_label_create(button);
  lv_label_set_text(state->label, text != NULL ? text : "BUTTON");
  lv_obj_center(state->label);
  lv_obj_set_style_text_color(state->label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(state->label, &lv_font_montserrat_16, 0);
  lv_obj_clear_flag(state->label, LV_OBJ_FLAG_CLICKABLE);

  state->timer = lv_timer_create(liquid_glass_button_timer_cb, 16, state);
  if(state->timer != NULL) lv_timer_pause(state->timer);
  lv_obj_add_event_cb(button, liquid_glass_button_event_cb,
                      LV_EVENT_PRESSED, state);
  lv_obj_add_event_cb(button, liquid_glass_button_event_cb,
                      LV_EVENT_PRESSING, state);
  lv_obj_add_event_cb(button, liquid_glass_button_event_cb,
                      LV_EVENT_RELEASED, state);
  lv_obj_add_event_cb(button, liquid_glass_button_event_cb,
                      LV_EVENT_PRESS_LOST, state);
  lv_obj_add_event_cb(button, liquid_glass_button_delete_cb,
                      LV_EVENT_DELETE, state);
  return button;
}

void liquid_glass_button_set_text(lv_obj_t * button, const char * text)
{
  uint32_t i;

  if(button == NULL || text == NULL) return;
  for(i = 0; i < lv_obj_get_event_count(button); i++) {
    lv_event_dsc_t * dsc = lv_obj_get_event_dsc(button, i);
    liquid_glass_button_state_t * state;

    if(dsc == NULL ||
       lv_event_dsc_get_cb(dsc) != liquid_glass_button_event_cb) continue;
    state = lv_event_dsc_get_user_data(dsc);
    if(state->button != button || state->label == NULL) continue;
    lv_label_set_text(state->label, text);
    lv_obj_center(state->label);
    return;
  }
}

void liquid_glass_button_set_tint(lv_obj_t * button,
                                  lv_color_t color,
                                  lv_opa_t opa)
{
  uint32_t i;

  if(button == NULL) return;
  for(i = 0; i < lv_obj_get_event_count(button); i++) {
    lv_event_dsc_t * dsc = lv_obj_get_event_dsc(button, i);
    liquid_glass_button_state_t * state;

    if(dsc == NULL ||
       lv_event_dsc_get_cb(dsc) != liquid_glass_button_event_cb) continue;
    state = lv_event_dsc_get_user_data(dsc);
    if(state->button != button) continue;

    state->tint_color = color;
    state->tint_opa = opa;
    liquid_glass_set_tint(button, color, opa);
    liquid_glass_mark_dirty(button);
    return;
  }
}
