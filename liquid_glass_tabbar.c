#include "liquid_glass_tabbar.h"

#include "liquid_glass_widget.h"

#include <math.h>
#include <stdint.h>

#define GLASS_TABBAR_INSET          4
#define GLASS_TABBAR_PRESS_SCALE    (78.0f / 56.0f)
#define GLASS_TABBAR_MAX_VELOCITY_STRETCH 0.12f
#define GLASS_TABBAR_MAX_PANEL_SHIFT 2.0f
#define GLASS_TABBAR_DRAG_FOLLOW_OMEGA 150.0f
#define GLASS_TABBAR_MAX_DRAG_LAG   0.3f
#define GLASS_TABBAR_PRESS_OMEGA    31.622776f
#define GLASS_TABBAR_SPRING_OMEGA   17.320508f
#define GLASS_TABBAR_SPRING_DAMP    0.5f
#define GLASS_TABBAR_DRAG_THRESHOLD 3

typedef struct liquid_glass_tabbar_state liquid_glass_tabbar_state_t;

typedef struct {
  liquid_glass_tabbar_state_t * state;
  lv_obj_t * item;
  lv_obj_t * icon;
  lv_obj_t * label;
  int32_t x;
  uint32_t index;
} liquid_glass_tab_item_t;

static void liquid_glass_tabbar_delete_cb(lv_event_t * e);

struct liquid_glass_tabbar_state {
  lv_obj_t * bar;
  lv_obj_t * glass;
  lv_obj_t * indicator;
  lv_timer_t * timer;
  liquid_glass_tab_item_t * items;
  uint32_t count;
  uint32_t active;
  float fraction;
  float fraction_velocity;
  float target_fraction;
  float press;
  float press_velocity;
  float target_press;
  float scale_x;
  float scale_x_velocity;
  float scale_y;
  float scale_y_velocity;
  float panel_offset;
  float panel_offset_velocity;
  float target_panel_offset;
  int32_t start_pointer_x;
  float drag_start_fraction;
  uint32_t last_tick;
  bool pressed;
  bool did_drag;
  bool release_when_settled;
};

static float clamp_float(float value, float min_value, float max_value)
{
  if(value < min_value) return min_value;
  if(value > max_value) return max_value;
  return value;
}

static float spring_step(float current, float velocity, float target,
                         float dt, float * next_velocity)
{
  float x0 = current - target;
  float omega_d = GLASS_TABBAR_SPRING_OMEGA *
                  sqrtf(1.0f - GLASS_TABBAR_SPRING_DAMP *
                        GLASS_TABBAR_SPRING_DAMP);
  float decay = expf(-GLASS_TABBAR_SPRING_DAMP *
                     GLASS_TABBAR_SPRING_OMEGA * dt);
  float cos_wd = cosf(omega_d * dt);
  float sin_wd = sinf(omega_d * dt);
  float b0 = (velocity + GLASS_TABBAR_SPRING_DAMP *
              GLASS_TABBAR_SPRING_OMEGA * x0) / omega_d;
  float offset = x0 * decay * cos_wd + b0 * decay * sin_wd;

  *next_velocity =
    -GLASS_TABBAR_SPRING_DAMP * GLASS_TABBAR_SPRING_OMEGA * offset +
    decay * (-x0 * omega_d * sin_wd + b0 * omega_d * cos_wd);
  return target + offset;
}

static float critical_spring_step(float current, float velocity, float target,
                                  float dt, float omega,
                                  float * next_velocity)
{
  float x0 = current - target;
  float decay = expf(-omega * dt);
  float c = velocity + omega * x0;
  float offset = (x0 + c * dt) * decay;

  *next_velocity = (velocity - omega * c * dt) * decay;
  return target + offset;
}

static liquid_glass_tabbar_state_t * get_tabbar_state(const lv_obj_t * bar)
{
  uint32_t i;

  if(bar == NULL) return NULL;
  for(i = 0; i < lv_obj_get_event_count(bar); i++) {
    lv_event_dsc_t * dsc = lv_obj_get_event_dsc((lv_obj_t *)bar, i);

    if(dsc == NULL) continue;
    /* The delete callback is a stable marker for this control's state. */
    if(lv_event_dsc_get_cb(dsc) == liquid_glass_tabbar_delete_cb) {
      return lv_event_dsc_get_user_data(dsc);
    }
  }
  return NULL;
}

static void update_tabbar_visual(liquid_glass_tabbar_state_t * state)
{
  int32_t width;
  int32_t height;
  int32_t inner_width;
  float tab_step;
  int32_t scale_x;
  int32_t scale_y;
  int32_t bar_scale;
  float bar_zoom;
  float velocity;
  float velocity_x;
  float velocity_y;
  float content_zoom;
  bool live_backdrop;
  liquid_glass_params_t * indicator_params;
  lv_opa_t tint_opa;
  uint32_t i;

  if(state == NULL || state->bar == NULL || state->indicator == NULL) return;

  width = lv_obj_get_width(state->bar);
  height = lv_obj_get_height(state->bar);
  inner_width = width - GLASS_TABBAR_INSET * 2;
  if(width <= 0 || height <= 0 || inner_width <= 0 || state->count == 0) return;

  tab_step = (float)inner_width / (float)state->count;
  if(tab_step < 1.0f) return;

  bar_zoom = 1.0f + (16.0f / (float)width) * state->press;
  bar_scale = (int32_t)lroundf(256.0f * bar_zoom);
  lv_obj_set_pos(state->glass, (int32_t)lroundf(state->panel_offset), 0);
  lv_obj_set_style_transform_scale_x(state->glass, bar_scale, 0);
  lv_obj_set_style_transform_scale_y(state->glass, bar_scale, 0);
  liquid_glass_set_view_scale_xy(state->glass, bar_scale, bar_scale);

  scale_x = (int32_t)lroundf(256.0f * state->scale_x);
  scale_y = (int32_t)lroundf(256.0f * state->scale_y);
  if(state->pressed) {
    velocity = state->fraction_velocity / 10.0f;
    velocity_x = clamp_float(velocity * 0.75f,
                             -GLASS_TABBAR_MAX_VELOCITY_STRETCH,
                             GLASS_TABBAR_MAX_VELOCITY_STRETCH);
    velocity_y = clamp_float(velocity * 0.25f,
                             -GLASS_TABBAR_MAX_VELOCITY_STRETCH,
                             GLASS_TABBAR_MAX_VELOCITY_STRETCH);
    scale_x = (int32_t)lroundf((float)scale_x / (1.0f - velocity_x));
    scale_y = (int32_t)lroundf((float)scale_y * (1.0f - velocity_y));
  }
  if(scale_x < 1) scale_x = 1;
  if(scale_y < 1) scale_y = 1;

  content_zoom = 1.0f + 0.04f * state->press;
  lv_obj_set_style_transform_scale_x(state->indicator, scale_x, 0);
  lv_obj_set_style_transform_scale_y(state->indicator, scale_y, 0);
  lv_obj_set_pos(state->indicator,
                 GLASS_TABBAR_INSET + (int32_t)lroundf(
                   state->fraction * tab_step + state->panel_offset),
                 GLASS_TABBAR_INSET);
  liquid_glass_set_view_scale_xy(
    state->indicator,
    scale_x,
    scale_y);
  tint_opa = (lv_opa_t)lroundf(26.0f * (1.0f -
    clamp_float(state->press, 0.0f, 1.0f)));
  liquid_glass_set_tint(state->indicator, lv_color_hex(0x142c45), tint_opa);
  live_backdrop = state->pressed || state->press > 0.02f;
  liquid_glass_set_live_backdrop(state->indicator, live_backdrop);
  lv_obj_set_style_bg_image_opa(state->indicator,
                                live_backdrop ? LV_OPA_COVER : 128, 0);
  lv_obj_set_style_shadow_opa(state->indicator,
                              (lv_opa_t)lroundf(state->press * 100.0f), 0);
  indicator_params = liquid_glass_get_params(state->indicator);
  if(indicator_params != NULL) {
    indicator_params->refraction_height = 10;
    indicator_params->refraction_amount = -14.0f * state->press;
    indicator_params->dispersion = 0.35f * state->press;
    indicator_params->vibrancy = 0.06f * state->press;
    indicator_params->highlight = 0.5f * state->press;
  }

  for(i = 0; i < state->count; i++) {
    liquid_glass_tab_item_t * item = &state->items[i];
    int32_t item_scale = (int32_t)lroundf(256.0f * content_zoom);

    lv_obj_set_pos(item->item,
                   item->x + (int32_t)lroundf(state->panel_offset),
                   GLASS_TABBAR_INSET);
    lv_obj_set_style_transform_scale_x(item->item, item_scale, 0);
    lv_obj_set_style_transform_scale_y(item->item, item_scale, 0);
    lv_obj_set_style_transform_pivot_x(item->item,
                                       lv_obj_get_width(item->item) / 2, 0);
    lv_obj_set_style_transform_pivot_y(
      item->item, height / 2 - GLASS_TABBAR_INSET, 0);
  }

  liquid_glass_mark_dirty(state->glass);
  liquid_glass_mark_dirty(state->indicator);
}

static void start_tabbar_timer(liquid_glass_tabbar_state_t * state)
{
  if(state == NULL || state->timer == NULL) return;
  state->last_tick = lv_tick_get();
  lv_timer_resume(state->timer);
  lv_timer_ready(state->timer);
}

static void select_tab(liquid_glass_tabbar_state_t * state,
                       uint32_t index,
                       bool animate,
                       bool notify)
{
  bool changed;

  if(state == NULL || index >= state->count) return;
  changed = state->active != index;
  state->active = index;
  state->target_fraction = (float)index;
  state->target_panel_offset = 0.0f;
  state->release_when_settled = animate;

  if(animate && state->timer != NULL) {
    state->target_press = 1.0f;
    start_tabbar_timer(state);
  }
  else {
    state->fraction = (float)index;
    state->fraction_velocity = 0.0f;
    state->press = 0.0f;
    state->press_velocity = 0.0f;
    state->target_press = 0.0f;
    state->scale_x = 1.0f;
    state->scale_x_velocity = 0.0f;
    state->scale_y = 1.0f;
    state->scale_y_velocity = 0.0f;
    state->panel_offset = 0.0f;
    state->panel_offset_velocity = 0.0f;
    state->release_when_settled = false;
    update_tabbar_visual(state);
    if(state->timer != NULL) lv_timer_pause(state->timer);
  }

  if(changed && notify) {
    lv_obj_send_event(state->bar, LV_EVENT_VALUE_CHANGED, NULL);
  }
}

static void liquid_glass_tabbar_timer_cb(lv_timer_t * timer)
{
  liquid_glass_tabbar_state_t * state = lv_timer_get_user_data(timer);
  uint32_t now;
  float dt;
  float scale_x_target;
  float scale_y_target;
  float delta;
  bool moving;

  if(state == NULL || state->bar == NULL || state->indicator == NULL) return;

  now = lv_tick_get();
  if(state->last_tick == 0) state->last_tick = now;
  dt = (float)(now - state->last_tick) / 1000.0f;
  state->last_tick = now;
  if(dt <= 0.0f) return;
  if(dt > 0.05f) dt = 0.05f;

  if(state->pressed && state->did_drag) {
    float lag;
    float max_drag_speed = GLASS_TABBAR_DRAG_FOLLOW_OMEGA *
                           GLASS_TABBAR_MAX_DRAG_LAG;

    /* Keep a small spring trail, bounded so the lens stays under the finger. */
    state->fraction = critical_spring_step(
      state->fraction, state->fraction_velocity, state->target_fraction, dt,
      GLASS_TABBAR_DRAG_FOLLOW_OMEGA, &state->fraction_velocity);
    state->fraction_velocity = clamp_float(state->fraction_velocity,
                                            -max_drag_speed,
                                            max_drag_speed);
    lag = state->target_fraction - state->fraction;
    if(lag > GLASS_TABBAR_MAX_DRAG_LAG) {
      state->fraction = state->target_fraction - GLASS_TABBAR_MAX_DRAG_LAG;
      if(state->fraction_velocity < 0.0f) state->fraction_velocity = 0.0f;
    }
    else if(lag < -GLASS_TABBAR_MAX_DRAG_LAG) {
      state->fraction = state->target_fraction + GLASS_TABBAR_MAX_DRAG_LAG;
      if(state->fraction_velocity > 0.0f) state->fraction_velocity = 0.0f;
    }
  }
  else {
    state->fraction = critical_spring_step(
      state->fraction, state->fraction_velocity, state->target_fraction, dt,
      31.622776f, &state->fraction_velocity);
  }
  state->press = critical_spring_step(
    state->press, state->press_velocity, state->target_press, dt,
    GLASS_TABBAR_PRESS_OMEGA, &state->press_velocity);
  state->panel_offset = critical_spring_step(
    state->panel_offset, state->panel_offset_velocity,
    state->target_panel_offset, dt, GLASS_TABBAR_SPRING_OMEGA,
    &state->panel_offset_velocity);
  if(state->panel_offset > GLASS_TABBAR_MAX_PANEL_SHIFT) {
    state->panel_offset = GLASS_TABBAR_MAX_PANEL_SHIFT;
    if(state->panel_offset_velocity > 0.0f) {
      state->panel_offset_velocity = 0.0f;
    }
  }
  else if(state->panel_offset < -GLASS_TABBAR_MAX_PANEL_SHIFT) {
    state->panel_offset = -GLASS_TABBAR_MAX_PANEL_SHIFT;
    if(state->panel_offset_velocity < 0.0f) {
      state->panel_offset_velocity = 0.0f;
    }
  }

  scale_x_target = state->pressed || state->release_when_settled
                     ? GLASS_TABBAR_PRESS_SCALE
                     : 1.0f;
  scale_y_target = scale_x_target;
  state->scale_x = spring_step(state->scale_x, state->scale_x_velocity,
                               scale_x_target, dt, &state->scale_x_velocity);
  state->scale_y = spring_step(state->scale_y, state->scale_y_velocity,
                               scale_y_target, dt, &state->scale_y_velocity);

  delta = fabsf(state->target_fraction - state->fraction);
  if(!state->pressed && state->release_when_settled && delta < 0.02f) {
    state->target_press = 0.0f;
    state->release_when_settled = false;
  }

  update_tabbar_visual(state);

  moving = fabsf(state->target_fraction - state->fraction) > 0.003f ||
           fabsf(state->fraction_velocity) > 0.003f ||
           fabsf(state->target_press - state->press) > 0.003f ||
           fabsf(state->press_velocity) > 0.003f ||
           fabsf(state->target_panel_offset - state->panel_offset) > 0.1f ||
           fabsf(state->panel_offset_velocity) > 0.5f ||
           fabsf(scale_x_target - state->scale_x) > 0.003f ||
           fabsf(state->scale_x_velocity) > 0.003f ||
           fabsf(scale_y_target - state->scale_y) > 0.003f ||
           fabsf(state->scale_y_velocity) > 0.003f;
  if(!moving) {
    state->fraction = state->target_fraction;
    state->fraction_velocity = 0.0f;
    state->press = state->target_press;
    state->press_velocity = 0.0f;
    state->scale_x = scale_x_target;
    state->scale_x_velocity = 0.0f;
    state->scale_y = scale_y_target;
    state->scale_y_velocity = 0.0f;
    state->panel_offset = state->target_panel_offset;
    state->panel_offset_velocity = 0.0f;
    update_tabbar_visual(state);
    lv_timer_pause(state->timer);
  }
}

static void tab_item_event_cb(lv_event_t * e)
{
  liquid_glass_tab_item_t * item = lv_event_get_user_data(e);
  liquid_glass_tabbar_state_t * state;
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev = lv_event_get_param(e);

  if(item == NULL || item->state == NULL) return;
  state = item->state;

  if(code == LV_EVENT_PRESSED) {
    lv_point_t point;

    state->pressed = true;
    state->did_drag = false;
    state->target_press = 1.0f;
    state->release_when_settled = false;
    state->drag_start_fraction = (float)state->active;
    if(indev != NULL) {
      lv_indev_get_point(indev, &point);
      state->start_pointer_x = point.x;
    }
    else {
      state->start_pointer_x = 0;
    }
    start_tabbar_timer(state);
  }
  else if(code == LV_EVENT_PRESSING && state->pressed && indev != NULL) {
    lv_point_t point;
    int32_t delta_x;
    int32_t inner_width;

    lv_indev_get_point(indev, &point);
    delta_x = point.x - state->start_pointer_x;
    if(delta_x > GLASS_TABBAR_DRAG_THRESHOLD ||
       delta_x < -GLASS_TABBAR_DRAG_THRESHOLD) {
      state->did_drag = true;
    }
    if(state->did_drag) {
      inner_width = lv_obj_get_width(state->bar) - GLASS_TABBAR_INSET * 2;
      if(inner_width < 1) inner_width = 1;
      state->target_fraction = clamp_float(
        state->drag_start_fraction +
          (float)delta_x * (float)state->count / (float)inner_width,
        0.0f, (float)(state->count - 1));

      {
        float offset_fraction = clamp_float(
          (float)delta_x / (float)inner_width, -1.0f, 1.0f);
        float eased = 1.0f - (1.0f - fabsf(offset_fraction)) *
                             (1.0f - fabsf(offset_fraction));
        state->target_panel_offset = GLASS_TABBAR_MAX_PANEL_SHIFT *
          (offset_fraction < 0.0f ? -1.0f : 1.0f) * eased;
      }
      start_tabbar_timer(state);
    }
  }
  else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    state->pressed = false;
    state->target_press = 1.0f;
    state->release_when_settled = true;
    state->target_panel_offset = 0.0f;
    if(state->did_drag) {
      uint32_t next_index = (uint32_t)lroundf(state->target_fraction);
      if(next_index >= state->count) next_index = state->count - 1;
      select_tab(state, next_index, true, true);
    }
    else {
      start_tabbar_timer(state);
    }
  }
  else if(code == LV_EVENT_CLICKED) {
    if(state->did_drag) return;
    select_tab(state, item->index, true, true);
  }
}

static void liquid_glass_tabbar_delete_cb(lv_event_t * e)
{
  liquid_glass_tabbar_state_t * state = lv_event_get_user_data(e);

  if(state == NULL) return;
  if(state->timer != NULL) lv_timer_del(state->timer);
  lv_free(state->items);
  lv_free(state);
}

lv_obj_t * liquid_glass_tabbar_create(lv_obj_t * parent,
                                      const char * const labels[],
                                      uint32_t tab_count,
                                      int32_t width,
                                      int32_t height)
{
  static const char * const default_labels[] = {
    "HOME", "GLASS", "MOTION", "INFO"
  };
  static const char * const icons[] = {
    LV_SYMBOL_HOME, LV_SYMBOL_IMAGE, LV_SYMBOL_SETTINGS, LV_SYMBOL_LIST
  };
  liquid_glass_tabbar_state_t * state;
  liquid_glass_params_t params;
  lv_obj_t * bar;
  uint32_t i;
  int32_t inner_width;
  int32_t inner_height;
  int32_t tab_width;

  if(parent == NULL || width <= GLASS_TABBAR_INSET * 2 ||
     height <= GLASS_TABBAR_INSET * 2 || tab_count == 0 ||
     tab_count > (uint32_t)(width - GLASS_TABBAR_INSET * 2)) return NULL;

  bar = lv_obj_create(parent);
  if(bar == NULL) return NULL;
  state = lv_malloc_zeroed(sizeof(*state));
  if(state == NULL) {
    lv_obj_delete(bar);
    return NULL;
  }
  state->items = lv_malloc_zeroed(sizeof(*state->items) * tab_count);
  if(state->items == NULL) {
    lv_free(state);
    lv_obj_delete(bar);
    return NULL;
  }

  state->bar = bar;
  state->count = tab_count;
  state->scale_x = 1.0f;
  state->scale_y = 1.0f;
  lv_obj_add_event_cb(bar, liquid_glass_tabbar_delete_cb, LV_EVENT_DELETE, state);

  lv_obj_set_size(bar, width, height);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_clip_corner(bar, false, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  state->glass = lv_obj_create(bar);
  if(state->glass == NULL) {
    lv_obj_delete(bar);
    return NULL;
  }
  lv_obj_set_size(state->glass, width, height);
  lv_obj_set_pos(state->glass, 0, 0);
  lv_obj_clear_flag(state->glass, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(state->glass, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(state->glass, height / 2, 0);
  lv_obj_set_style_clip_corner(state->glass, true, 0);
  lv_obj_set_style_bg_opa(state->glass, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(state->glass, 0, 0);
  lv_obj_set_style_pad_all(state->glass, 0, 0);
  lv_obj_set_style_transform_pivot_x(state->glass, width / 2, 0);
  lv_obj_set_style_transform_pivot_y(state->glass, height / 2, 0);
  lv_obj_set_style_shadow_color(state->glass, lv_color_hex(0x173553), 0);
  lv_obj_set_style_shadow_width(state->glass, 14, 0);
  lv_obj_set_style_shadow_offset_y(state->glass, 5, 0);
  lv_obj_set_style_shadow_opa(state->glass, 70, 0);

  liquid_glass_params_init(&params);
  params.refraction_height = 24;
  params.refraction_amount = -24.0f;
  params.corner_radius = height / 2;
  params.vibrancy = 0.12f;
  params.highlight = 0.5f;
  if(!liquid_glass_apply(state->glass, &params)) {
    lv_obj_delete(bar);
    return NULL;
  }
  /* The base should sample behind the whole control, leaving tab content crisp. */
  liquid_glass_set_hide_target(state->glass, bar);
  liquid_glass_set_blur(state->glass, 8);
  liquid_glass_set_tint(state->glass, lv_color_hex(0xfafafa), 82);

  inner_width = width - GLASS_TABBAR_INSET * 2;
  inner_height = height - GLASS_TABBAR_INSET * 2;
  tab_width = (int32_t)lroundf((float)inner_width / (float)tab_count);
  if(tab_width < 1) tab_width = 1;
  {
    float max_scale_x = GLASS_TABBAR_PRESS_SCALE /
                        (1.0f - GLASS_TABBAR_MAX_VELOCITY_STRETCH);
    float max_scale_y = GLASS_TABBAR_PRESS_SCALE *
                        (1.0f + GLASS_TABBAR_MAX_VELOCITY_STRETCH);
    /* Overflow-visible children still need a parent draw-area margin. */
    int32_t width_overflow = (int32_t)ceilf(
      (float)tab_width * (max_scale_x - 1.0f) * 0.5f + 18.0f);
    int32_t height_overflow = (int32_t)ceilf(
      (float)inner_height * (max_scale_y - 1.0f) * 0.5f + 18.0f);
    int32_t overflow = width_overflow > height_overflow
                         ? width_overflow
                         : height_overflow;

    if(overflow < 32) overflow = 32;
    lv_obj_set_ext_draw_size(bar, overflow);
    lv_obj_refresh_ext_draw_size(bar);
  }

  for(i = 0; i < tab_count; i++) {
    liquid_glass_tab_item_t * item = &state->items[i];
    int32_t item_left = ((int32_t)i * inner_width) / (int32_t)tab_count;
    int32_t item_right = ((int32_t)(i + 1) * inner_width) /
                         (int32_t)tab_count;
    int32_t x = GLASS_TABBAR_INSET + item_left;
    int32_t item_width = item_right - item_left;
    const char * label_text = labels != NULL && labels[i] != NULL
                                ? labels[i]
                                : (i < sizeof(default_labels) /
                                      sizeof(default_labels[0])
                                     ? default_labels[i]
                                     : "TAB");
    const char * icon_text = icons[i % (sizeof(icons) / sizeof(icons[0]))];

    item->state = state;
    item->index = i;
    item->x = x;
    item->item = lv_obj_create(bar);
    if(item->item == NULL) {
      lv_obj_delete(bar);
      return NULL;
    }
    lv_obj_set_size(item->item, item_width, inner_height);
    lv_obj_set_pos(item->item, x, GLASS_TABBAR_INSET);
    lv_obj_clear_flag(item->item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item->item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(item->item, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_opa(item->item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item->item, 0, 0);
    lv_obj_set_style_pad_all(item->item, 0, 0);
    lv_obj_set_style_radius(item->item, height / 2, 0);
    lv_obj_add_event_cb(item->item, tab_item_event_cb, LV_EVENT_PRESSED, item);
    lv_obj_add_event_cb(item->item, tab_item_event_cb, LV_EVENT_PRESSING, item);
    lv_obj_add_event_cb(item->item, tab_item_event_cb, LV_EVENT_RELEASED, item);
    lv_obj_add_event_cb(item->item, tab_item_event_cb, LV_EVENT_PRESS_LOST, item);
    lv_obj_add_event_cb(item->item, tab_item_event_cb, LV_EVENT_CLICKED, item);

    item->icon = lv_label_create(item->item);
    if(item->icon == NULL) {
      lv_obj_delete(bar);
      return NULL;
    }
    lv_label_set_text(item->icon, icon_text);
    lv_obj_set_width(item->icon, item_width);
    lv_obj_set_pos(item->icon, 0, 4);
    lv_obj_set_style_text_align(item->icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(item->icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(item->icon, lv_color_hex(0xffffff), 0);
    lv_obj_clear_flag(item->icon, LV_OBJ_FLAG_CLICKABLE);

    item->label = lv_label_create(item->item);
    if(item->label == NULL) {
      lv_obj_delete(bar);
      return NULL;
    }
    lv_label_set_text(item->label, label_text);
    lv_obj_set_width(item->label, item_width);
    lv_obj_set_pos(item->label, 0, inner_height - 19);
    lv_obj_set_style_text_align(item->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(item->label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(item->label, lv_color_hex(0xffffff), 0);
    lv_obj_clear_flag(item->label, LV_OBJ_FLAG_CLICKABLE);
  }

  state->indicator = lv_obj_create(bar);
  if(state->indicator == NULL) {
    lv_obj_delete(bar);
    return NULL;
  }
  lv_obj_set_size(state->indicator, tab_width, inner_height);
  lv_obj_set_pos(state->indicator, GLASS_TABBAR_INSET, GLASS_TABBAR_INSET);
  lv_obj_clear_flag(state->indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(state->indicator, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(state->indicator, inner_height / 2, 0);
  lv_obj_set_style_clip_corner(state->indicator, true, 0);
  lv_obj_set_style_bg_opa(state->indicator, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(state->indicator, 0, 0);
  lv_obj_set_style_pad_all(state->indicator, 0, 0);
  lv_obj_set_style_transform_pivot_y(state->indicator, inner_height / 2, 0);
  lv_obj_set_style_transform_pivot_x(state->indicator, tab_width / 2, 0);
  lv_obj_set_style_shadow_color(state->indicator, lv_color_hex(0x0c1e32), 0);
  lv_obj_set_style_shadow_width(state->indicator, 12, 0);
  lv_obj_set_style_shadow_offset_y(state->indicator, 4, 0);
  lv_obj_set_style_shadow_opa(state->indicator, 0, 0);

  params.refraction_height = 10;
  params.refraction_amount = 0.0f;
  params.corner_radius = inner_height / 2;
  params.dispersion = 0.35f;
  params.vibrancy = 0.0f;
  params.highlight = 0.0f;
  if(!liquid_glass_apply(state->indicator, &params)) {
    lv_obj_delete(bar);
    return NULL;
  }
  liquid_glass_set_blur(state->indicator, 0);
  liquid_glass_set_tint(state->indicator, lv_color_hex(0x142c45), 26);

  state->timer = lv_timer_create(liquid_glass_tabbar_timer_cb, 16, state);
  if(state->timer != NULL) lv_timer_pause(state->timer);
  update_tabbar_visual(state);
  return bar;
}

void liquid_glass_tabbar_set_active(lv_obj_t * tabbar,
                                    uint32_t index,
                                    lv_anim_enable_t anim_en)
{
  liquid_glass_tabbar_state_t * state = get_tabbar_state(tabbar);

  if(state == NULL || index >= state->count) return;
  state->pressed = false;
  state->did_drag = false;
  select_tab(state, index, anim_en == LV_ANIM_ON, true);
}

uint32_t liquid_glass_tabbar_get_active(const lv_obj_t * tabbar)
{
  liquid_glass_tabbar_state_t * state = get_tabbar_state(tabbar);

  return state != NULL ? state->active : 0;
}
