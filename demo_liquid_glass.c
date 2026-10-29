/**
 * @file demo_liquid_glass.c
 *
 * Original liquid glass showcase: a draggable glass panel with parameter
 * sliders and a glass button over a live backdrop, built on the reusable
 * liquid_glass_widget layer.
 */

#include "demo_liquid_glass.h"
#include "liquid_glass_widget.h"
#include "lvgl/lvgl.h"

LV_IMAGE_DECLARE(img);

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
  GLASS_CONTROL_OPACITY,
  GLASS_CONTROL_BLUR,
  GLASS_CONTROL_REFRACTION_HEIGHT,
  GLASS_CONTROL_REFRACTION_AMOUNT,
  GLASS_CONTROL_DISPERSION,
  GLASS_CONTROL_CORNER_RADIUS
} glass_control_type_t;

typedef struct {
  lv_obj_t * glass;
  lv_obj_t * value_label;
  glass_control_type_t type;
} glass_control_t;

typedef struct {
  lv_obj_t * glass;
  lv_point_t grab_offset;
} glass_drag_state_t;

typedef struct {
  lv_obj_t * bar;
  lv_obj_t * value_label;
  int32_t value;
  int32_t direction;
} backdrop_animation_t;

typedef struct {
  lv_obj_t * button;
} glass_button_state_t;

/**********************
 *  STATIC VARIABLES
 **********************/

static backdrop_animation_t backdrop_animation;

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void glass_drag_event_cb(lv_event_t * e)
{
  glass_drag_state_t * drag = lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev = lv_event_get_param(e);
  lv_point_t point;

  if(drag == NULL || drag->glass == NULL || indev == NULL) return;

  lv_indev_get_point(indev, &point);

  if(code == LV_EVENT_PRESSED) {
    lv_area_t glass_coords;

    lv_obj_get_coords(drag->glass, &glass_coords);
    drag->grab_offset.x = point.x - glass_coords.x1;
    drag->grab_offset.y = point.y - glass_coords.y1;
  }
  else if(code == LV_EVENT_PRESSING) {
    lv_obj_t * parent = lv_obj_get_parent(drag->glass);
    lv_area_t parent_coords;

    lv_obj_get_coords(parent, &parent_coords);
    lv_obj_set_pos(
      drag->glass,
      point.x - parent_coords.x1 - drag->grab_offset.x,
      point.y - parent_coords.y1 - drag->grab_offset.y
    );

    /* Keep the dragged glass in lockstep with the pointer; everything it
     * overlaps follows on the next manager tick. */
    liquid_glass_refresh(drag->glass);
  }
}

static void glass_slider_event_cb(lv_event_t * e)
{
  glass_control_t * control = lv_event_get_user_data(e);
  lv_obj_t * slider = lv_event_get_target(e);
  liquid_glass_params_t * params;
  int32_t value;
  char value_text[16];

  if(control == NULL || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  value = lv_slider_get_value(slider);
  params = liquid_glass_get_params(control->glass);

  if(control->type == GLASS_CONTROL_OPACITY) {
    liquid_glass_set_tint(control->glass, lv_color_hex(0xb8dcff), (lv_opa_t)value);
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d%%", value * 100 / 255);
  }
  else if(control->type == GLASS_CONTROL_BLUR) {
    liquid_glass_set_blur(control->glass, value);
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d px", value);
  }
  else if(control->type == GLASS_CONTROL_REFRACTION_HEIGHT) {
    if(params != NULL) params->refraction_height = value;
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d px", value);
  }
  else if(control->type == GLASS_CONTROL_REFRACTION_AMOUNT) {
    if(params != NULL) params->refraction_amount = (float)value;
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d px", value);
  }
  else if(control->type == GLASS_CONTROL_CORNER_RADIUS) {
    if(params != NULL) params->corner_radius = value;
    lv_obj_set_style_radius(control->glass, value, 0);
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d px", value);
  }
  else {
    if(params != NULL) params->dispersion = (float)value / 10.0f;
    liquid_glass_refresh(control->glass);
    lv_snprintf(value_text, sizeof(value_text), "%d.%d",
                value / 10, value % 10);
  }

  lv_label_set_text(control->value_label, value_text);
}

static lv_obj_t * create_glass_slider(lv_obj_t * parent,
                                      const char * name,
                                      const char * initial_value,
                                      int32_t min,
                                      int32_t max,
                                      int32_t value,
                                      glass_control_t * control)
{
  lv_obj_t * name_label = lv_label_create(parent);
  lv_obj_t * value_label = lv_label_create(parent);
  lv_obj_t * slider = lv_slider_create(parent);

  lv_label_set_text(name_label, name);
  lv_label_set_text(value_label, initial_value);
  int32_t row_y = 76 + (int32_t)control->type * 43;
  lv_obj_set_pos(name_label, 18, row_y);
  lv_obj_set_pos(value_label, 242, row_y);
  lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_color(value_label, lv_color_hex(0xffffff), 0);

  lv_obj_set_size(slider, 138, 12);
  lv_obj_set_pos(slider, 88, row_y + 5);
  lv_slider_set_range(slider, min, max);
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, 65, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, 220, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, 255, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(slider, 6, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(slider, 80, LV_PART_KNOB);

  control->value_label = value_label;
  lv_obj_add_event_cb(slider, glass_slider_event_cb, LV_EVENT_VALUE_CHANGED, control);
  return slider;
}

static lv_obj_t * create_liquid_glass_panel(lv_obj_t * parent)
{
  static glass_drag_state_t drag;
  static glass_control_t controls[6];
  lv_obj_t * glass = lv_obj_create(parent);
  lv_obj_t * handle;
  lv_obj_t * title;
  lv_obj_t * hint;

  lv_obj_set_size(glass, 290, 370);
  lv_obj_set_style_align(glass, LV_ALIGN_TOP_LEFT, 0);
  lv_obj_set_pos(glass, 15, 60);
  lv_obj_clear_flag(glass, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(glass, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(glass, LV_DIR_NONE);
  lv_obj_set_style_radius(glass, 28, 0);
  lv_obj_set_style_pad_all(glass, 0, 0);
  lv_obj_set_style_bg_opa(glass, 0, 0);
  lv_obj_set_style_border_width(glass, 0, 0);
  lv_obj_set_style_shadow_color(glass, lv_color_hex(0x15385f), 0);
  lv_obj_set_style_shadow_width(glass, 22, 0);
  lv_obj_set_style_shadow_offset_y(glass, 9, 0);
  lv_obj_set_style_shadow_opa(glass, 110, 0);

  /* Turn the plain container into liquid glass. */
  liquid_glass_apply(glass, NULL);
  liquid_glass_set_blur(glass, 2);
  liquid_glass_set_tint(glass, lv_color_hex(0xb8dcff), 82);

  drag.glass = glass;

  handle = lv_obj_create(glass);
  lv_obj_set_size(handle, 258, 52);
  lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scrollbar_mode(handle, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(handle, LV_DIR_NONE);
  lv_obj_set_style_bg_opa(handle, 0, 0);
  lv_obj_set_style_border_width(handle, 0, 0);
  lv_obj_set_style_pad_all(handle, 0, 0);
  lv_obj_add_event_cb(handle, glass_drag_event_cb, LV_EVENT_PRESSED, &drag);
  lv_obj_add_event_cb(handle, glass_drag_event_cb, LV_EVENT_PRESSING, &drag);

  title = lv_label_create(handle);
  lv_label_set_text(title, "LIQUID GLASS");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 1);
  lv_obj_add_event_cb(title, glass_drag_event_cb, LV_EVENT_PRESSED, &drag);
  lv_obj_add_event_cb(title, glass_drag_event_cb, LV_EVENT_PRESSING, &drag);
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_letter_space(title, 2, 0);

  hint = lv_label_create(handle);
  lv_label_set_text(hint, "drag the glass to move it");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 27);
  lv_obj_add_event_cb(hint, glass_drag_event_cb, LV_EVENT_PRESSED, &drag);
  lv_obj_add_event_cb(hint, glass_drag_event_cb, LV_EVENT_PRESSING, &drag);
  lv_obj_set_style_text_color(hint, lv_color_hex(0xe5f3ff), 0);
  lv_obj_set_style_text_opa(hint, 210, 0);

  controls[0] = (glass_control_t){ glass, NULL, GLASS_CONTROL_OPACITY };
  controls[1] = (glass_control_t){ glass, NULL, GLASS_CONTROL_BLUR };
  controls[2] = (glass_control_t){ glass, NULL, GLASS_CONTROL_REFRACTION_HEIGHT };
  controls[3] = (glass_control_t){ glass, NULL, GLASS_CONTROL_REFRACTION_AMOUNT };
  controls[4] = (glass_control_t){ glass, NULL, GLASS_CONTROL_DISPERSION };
  controls[5] = (glass_control_t){ glass, NULL, GLASS_CONTROL_CORNER_RADIUS };

  create_glass_slider(glass, "Opacity", "32%", 20, 180, 82, &controls[0]);
  create_glass_slider(glass, "Blur", "2 px", 0, 12, 2, &controls[1]);
  create_glass_slider(
    glass, "Refraction", "24 px", 0, 60, 24, &controls[2]
  );
  create_glass_slider(
    glass, "Amount", "24 px", 0, 60, 24, &controls[3]
  );
  create_glass_slider(
    glass, "Dispersion", "1.0", 0, 30, 10, &controls[4]
  );
  create_glass_slider(
    glass, "Radius", "28 px", 0, 64, 28, &controls[5]
  );

  return glass;
}

static void glass_button_scale_cb(void * var, int32_t value)
{
  glass_button_state_t * state = var;

  if(state == NULL || state->button == NULL) return;
  lv_obj_set_style_transform_scale(state->button, value, 0);
  /* Keep the refracted backdrop pinned while the button springs. */
  liquid_glass_set_view_scale_xy(state->button, value, value);
}

static void glass_button_anim(glass_button_state_t * state, int32_t end,
                              lv_anim_path_cb_t path)
{
  lv_anim_t anim;

  lv_anim_init(&anim);
  lv_anim_set_var(&anim, state);
  lv_anim_set_exec_cb(&anim, glass_button_scale_cb);
  lv_anim_set_values(&anim,
                     lv_obj_get_style_transform_scale_x(state->button, 0),
                     end);
  lv_anim_set_duration(&anim, 180);
  lv_anim_set_path_cb(&anim, path);
  lv_anim_start(&anim);
}

static void glass_button_event_cb(lv_event_t * e)
{
  glass_button_state_t * state = lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * button;

  if(state == NULL || state->button == NULL) return;
  button = state->button;

  if(code == LV_EVENT_PRESSED) {
    glass_button_anim(state, 238, lv_anim_path_ease_out);
    liquid_glass_set_tint(button, lv_color_hex(0xffffff), 145);
    liquid_glass_refresh(button);
  }
  else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    glass_button_anim(state, LV_SCALE_NONE, lv_anim_path_overshoot);
    liquid_glass_set_tint(button, lv_color_hex(0xffffff), 105);
    liquid_glass_refresh(button);
  }
}

static void glass_button_delete_cb(lv_event_t * e)
{
  glass_button_state_t * state = lv_event_get_user_data(e);

  if(state == NULL) return;
  lv_anim_delete(state, glass_button_scale_cb);
  lv_free(state);
}

static lv_obj_t * create_liquid_glass_button(lv_obj_t * parent,
                                             const char * text,
                                             int32_t x,
                                             int32_t y)
{
  glass_button_state_t * state = lv_malloc(sizeof(*state));
  lv_obj_t * button = lv_obj_create(parent);
  lv_obj_t * label = lv_label_create(button);
  liquid_glass_params_t * params;

  lv_obj_set_size(button, 140, 52);
  lv_obj_set_pos(button, x, y);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(button, 26, 0);
  lv_obj_set_style_bg_opa(button, 0, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_transform_pivot_x(button, 70, 0);
  lv_obj_set_style_transform_pivot_y(button, 26, 0);
  lv_obj_set_style_shadow_width(button, 14, 0);
  lv_obj_set_style_shadow_opa(button, 90, 0);

  /* Any widget works: apply the glass and give it a white tint that
     matches the old translucent background. */
  liquid_glass_apply(button, NULL);
  params = liquid_glass_get_params(button);
  if(params != NULL) {
    params->refraction_height = 18;
    params->refraction_amount = 12.0f;
    params->dispersion = 0.35f;
    params->corner_radius = 26;
  }
  liquid_glass_set_blur(button, 3);
  liquid_glass_set_tint(button, lv_color_hex(0xffffff), 105);

  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

  if(state == NULL) return button;
  state->button = button;
  lv_obj_add_event_cb(button, glass_button_event_cb,
                      LV_EVENT_PRESSED, state);
  lv_obj_add_event_cb(button, glass_button_event_cb,
                      LV_EVENT_RELEASED, state);
  lv_obj_add_event_cb(button, glass_button_event_cb,
                      LV_EVENT_PRESS_LOST, state);
  lv_obj_add_event_cb(button, glass_button_delete_cb,
                      LV_EVENT_DELETE, state);
  return button;
}

static void create_backdrop_content(lv_obj_t * screen)
{
  lv_obj_t * title = lv_label_create(screen);
  lv_obj_t * subtitle = lv_label_create(screen);
  lv_obj_t * card = lv_obj_create(screen);
  lv_obj_t * card_label = lv_label_create(card);
  lv_obj_t * progress_bar = lv_bar_create(card);

  lv_label_set_text(title, "LVGL BACKDROP");
  lv_obj_set_pos(title, 18, 18);
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_label_set_text(subtitle, "Text and widgets behind the liquid glass");
  lv_obj_set_pos(subtitle, 18, 44);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xe8f4ff), 0);
  lv_obj_set_style_text_opa(subtitle, 220, 0);

  lv_obj_set_size(card, 220, 68);
  lv_obj_set_pos(card, 46, 110);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2b75d6), 0);
  lv_obj_set_style_bg_opa(card, 210, 0);
  lv_obj_set_style_border_width(card, 0, 0);

  lv_label_set_text(card_label, "LIVE CONTENT  0%");
  lv_obj_align(card_label, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_set_style_text_color(card_label, lv_color_hex(0xffffff), 0);

  lv_obj_set_size(progress_bar, 184, 8);
  lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_MID, 0, -7);
  lv_bar_set_range(progress_bar, 0, 100);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progress_bar, 70, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x75f0c2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_INDICATOR);
  backdrop_animation.bar = progress_bar;
  backdrop_animation.value_label = card_label;
  backdrop_animation.value = 0;
  backdrop_animation.direction = 1;

  subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "Realtime snapshot / refraction / dispersion");
  lv_obj_set_pos(subtitle, 18, 476);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(subtitle, 230, 0);
}

static void backdrop_animation_timer_cb(lv_timer_t * timer)
{
  backdrop_animation_t * animation = lv_timer_get_user_data(timer);

  if(animation == NULL || animation->bar == NULL ||
     animation->value_label == NULL) return;

  animation->value += animation->direction * 7;
  if(animation->value >= 100) {
    animation->value = 100;
    animation->direction = -1;
  }
  else if(animation->value <= 0) {
    animation->value = 0;
    animation->direction = 1;
  }

  lv_bar_set_value(animation->bar, animation->value, LV_ANIM_OFF);
  lv_label_set_text_fmt(animation->value_label,
                        "LIVE CONTENT  %d%%",
                        animation->value);
  /* No manual glass refresh needed: the manager picks up the change. */
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void demo_liquid_glass_run(void)
{
  lv_obj_t * screen = lv_screen_active();

  /* Real-time mode: every glass surface re-renders each manager tick, so
   * stacked glass widgets and live backdrops stay in sync by themselves. */
  /* GPU (LIQUID_GLASS_RENDER_AUTO / LIQUID_GLASS_RENDER_SHADER) can be
   * re-enabled here once the desired look is tuned on the CPU path. */
  liquid_glass_set_render_mode(LIQUID_GLASS_RENDER_CPU);
  liquid_glass_set_refresh_policy(LIQUID_GLASS_REFRESH_ALWAYS);
  liquid_glass_set_refresh_period(33);

  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(screen, LV_DIR_NONE);

  lv_obj_t *background = lv_image_create(screen);
  lv_image_set_src(background, &img);
  lv_obj_center(background);
  lv_obj_move_background(background);

  create_backdrop_content(screen);

  create_liquid_glass_panel(screen);
  create_liquid_glass_button(screen, "GLASS BUTTON", 90, 422);
  lv_timer_create(backdrop_animation_timer_cb, 120, &backdrop_animation);
}
