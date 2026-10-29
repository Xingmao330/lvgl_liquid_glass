/**
 * @file demo_control_center.c
 *
 * A pull-down control center and a settings shortcut sit over the wallpaper.
 * Glass buttons, a media tile and two capsule sliders make up the controls.
 */

#include "demo_control_center.h"
#include "demo_settings.h"
#include "liquid_glass_button.h"
#include "liquid_glass_interactive.h"
#include "liquid_glass_widget.h"
#include "lvgl/lvgl.h"

LV_IMAGE_DECLARE(img);

#define CC_SCREEN_W        320
#define CC_SCREEN_H        483
#define CC_PULL_EDGE_H      42
#define CC_PANEL_HIDDEN_Y  (-CC_SCREEN_H - CC_PULL_EDGE_H)
#define CC_PULL_THRESHOLD   96
#define CC_CONTENT_PULL_RANGE 160
#define CC_CONTENT_PULL_SHIFT 24
#define CC_GESTURE_CLICK_SLOP 8
#define CC_FLAG_SLIDER      LV_OBJ_FLAG_USER_1
#define CC_SLIDER_Y         224
#define CC_SLIDER_H         140
#define CC_HOME_BUTTON_SIZE 56

/* LVGL scale unit: 256 == 100%. */
#define CC_SCALE_NONE      LV_SCALE_NONE
#define CC_STRETCH_Y       259    /* subtle slider stretch while pressed */

/* Layout of the sheet content. */
#define CC_TILE_R          36     /* big tile radius */

static bool cc_panel_drag_claimed;
static int32_t cc_content_offset;

/**********************
 *   SPRING ANIMATIONS
 **********************/

static void scale_y_exec_cb(void * var, int32_t value)
{
  lv_obj_t * obj = var;
  int32_t scale_x = CC_SCALE_NONE - (value - CC_SCALE_NONE);

  lv_obj_set_style_transform_scale_y(obj, value, 0);
  lv_obj_set_style_transform_scale_x(obj, scale_x, 0);
  liquid_glass_set_view_scale_xy(obj, scale_x, value);
  liquid_glass_refresh(obj);
}

static void spring_anim(lv_obj_t * obj, lv_anim_exec_xcb_t exec_cb,
                        int32_t end, lv_anim_path_cb_t path, uint32_t duration)
{
  lv_anim_t anim;

  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_exec_cb(&anim, exec_cb);
  lv_anim_set_values(&anim,
                     lv_obj_get_style_transform_scale_x(obj, 0),
                     end);
  lv_anim_set_duration(&anim, duration);
  lv_anim_set_path_cb(&anim, path);
  lv_anim_start(&anim);
}

/* Slider flavor: stretch vertically while pressed, wobble back on release. */
static void slider_press_anim(lv_obj_t * obj)
{
  spring_anim(obj, scale_y_exec_cb, CC_STRETCH_Y, lv_anim_path_ease_out, 140);
}

static void slider_release_anim(lv_obj_t * obj)
{
  spring_anim(obj, scale_y_exec_cb, CC_SCALE_NONE, lv_anim_path_overshoot, 480);
}

static void panel_y_exec_cb(void * var, int32_t value)
{
  lv_obj_t * panel = var;

  lv_obj_set_y(panel, value);
  liquid_glass_mark_dirty(panel);
}

static void content_offset_exec_cb(void * var, int32_t value)
{
  lv_obj_t * content = var;
  uint32_t i;

  if(value == cc_content_offset) return;

  lv_obj_set_y(content, value);
  cc_content_offset = value;
  for(i = 0; i < lv_obj_get_child_count(content); i++) {
    lv_obj_t * child = lv_obj_get_child(content, i);

    liquid_glass_mark_dirty(child);
  }
}

static void panel_set_drag_state(lv_obj_t * panel, lv_obj_t * content,
                                 int32_t y, int32_t extra_pull)
{
  int32_t content_shift;

  if(extra_pull < 0) extra_pull = 0;
  if(extra_pull > CC_CONTENT_PULL_RANGE) {
    extra_pull = CC_CONTENT_PULL_RANGE;
  }

  content_shift = CC_CONTENT_PULL_SHIFT * extra_pull /
                  CC_CONTENT_PULL_RANGE;
  if(lv_obj_get_y(panel) != y) {
    lv_obj_set_y(panel, y);
    liquid_glass_mark_dirty(panel);
  }
  content_offset_exec_cb(content, content_shift);
}

static void panel_snap(lv_obj_t * panel, lv_obj_t * content,
                       int32_t target_y)
{
  lv_anim_t move_anim;
  lv_anim_t content_anim;

  lv_anim_delete(panel, panel_y_exec_cb);
  lv_anim_delete(content, content_offset_exec_cb);

  if(lv_obj_get_y(panel) != target_y) {
    lv_anim_init(&move_anim);
    lv_anim_set_var(&move_anim, panel);
    lv_anim_set_exec_cb(&move_anim, panel_y_exec_cb);
    lv_anim_set_values(&move_anim, lv_obj_get_y(panel), target_y);
    lv_anim_set_duration(&move_anim, 400);
    lv_anim_set_path_cb(&move_anim, lv_anim_path_ease_out);
    lv_anim_start(&move_anim);
  }

  lv_anim_init(&content_anim);
  lv_anim_set_var(&content_anim, content);
  lv_anim_set_exec_cb(&content_anim, content_offset_exec_cb);
  lv_anim_set_values(&content_anim, cc_content_offset, 0);
  lv_anim_set_duration(&content_anim, 420);
  lv_anim_set_path_cb(&content_anim, lv_anim_path_overshoot);
  lv_anim_start(&content_anim);
}

/**********************
 *   GLASS TILE FACTORY
 **********************/

typedef struct {
  int32_t refraction_height;
  int32_t refraction_amount;
  int32_t blur;
  lv_opa_t tint_opa;
} tile_style_t;

/* Apply liquid glass with iOS-ish small-refraction defaults. */
static void tile_make_glass(lv_obj_t * obj, const tile_style_t * style)
{
  liquid_glass_params_t * params;

  liquid_glass_apply(obj, NULL);
  params = liquid_glass_get_params(obj);
  if(params != NULL) {
    params->refraction_height = style->refraction_height;
    params->refraction_amount = (float)style->refraction_amount;
    params->dispersion = 0.3f;
  }
  liquid_glass_set_blur(obj, style->blur);
  liquid_glass_set_tint(obj, lv_color_hex(0xffffff), style->tint_opa);
}

static void cc_button_set_glass_style(lv_obj_t * button,
                                      int32_t refraction_height,
                                      int32_t refraction_amount,
                                      int32_t blur,
                                      lv_color_t tint,
                                      lv_opa_t tint_opa)
{
  liquid_glass_params_t * params = liquid_glass_get_params(button);

  if(params != NULL) {
    params->refraction_height = refraction_height;
    params->refraction_amount = (float)refraction_amount;
    params->dispersion = 0.3f;
    params->corner_radius = lv_obj_get_height(button) / 2;
  }
  liquid_glass_set_blur(button, blur);
  liquid_glass_button_set_tint(button, tint, tint_opa);
  lv_obj_set_style_shadow_width(button, 8, 0);
  lv_obj_set_style_shadow_opa(button, 60, 0);
}

/* Slider capsule surface with backdrop compensation. */
static lv_obj_t * cc_tile_create(lv_obj_t * parent,
                                 int32_t x, int32_t y,
                                 int32_t w, int32_t h, int32_t radius,
                                 const tile_style_t * style)
{
  lv_obj_t * tile = lv_obj_create(parent);

  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, w, h);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_radius(tile, radius, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_clip_corner(tile, true, 0);
  /* The style pivot defaults to the top-left corner; scale about the
   * center so the spring squash stays symmetric and the backdrop
   * compensation in the widget layer lines up. */
  lv_obj_set_style_transform_pivot_x(tile, w / 2, 0);
  lv_obj_set_style_transform_pivot_y(tile, h / 2, 0);

  tile_make_glass(tile, style);
  {
    liquid_glass_params_t * params = liquid_glass_get_params(tile);

    if(params != NULL) params->corner_radius = radius;
  }
  return tile;
}

/**********************
 *   ROUND TOGGLES
 **********************/

static void round_toggle_cb(lv_event_t * e)
{
  lv_obj_t * button = lv_event_get_current_target(e);

  if(button == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if(cc_panel_drag_claimed) return;
  if(lv_obj_has_state(button, LV_STATE_CHECKED)) {
    lv_obj_clear_state(button, LV_STATE_CHECKED);
  }
  else {
    lv_obj_add_state(button, LV_STATE_CHECKED);
  }
}

/* A standard LVGL button with a translucent fill, not a glass surface. */
static lv_obj_t * cc_round_toggle_create(lv_obj_t * parent,
                                         int32_t x, int32_t y,
                                         int32_t size,
                                         const char * symbol,
                                         bool start_on)
{
  lv_obj_t * knob = lv_button_create(parent);
  lv_obj_t * label = lv_label_create(knob);

  lv_obj_set_pos(knob, x, y);
  lv_obj_set_size(knob, size, size);
  lv_obj_add_flag(knob, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_style_radius(knob, size / 2, 0);
  lv_obj_set_style_border_width(knob, 0, 0);
  lv_obj_set_style_shadow_width(knob, 0, 0);
  lv_obj_set_style_bg_color(knob, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(knob, 72, 0);
  lv_obj_set_style_bg_color(knob, lv_color_hex(0x0a84ff), LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(knob, 155, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(knob, lv_color_hex(0xffffff), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(knob, 125, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(knob, lv_color_hex(0x0a84ff),
                            LV_STATE_CHECKED | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(knob, 175,
                          LV_STATE_CHECKED | LV_STATE_PRESSED);

  if(start_on) lv_obj_add_state(knob, LV_STATE_CHECKED);

  lv_label_set_text(label, symbol);
  lv_obj_center(label);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

  lv_obj_add_event_cb(knob, round_toggle_cb, LV_EVENT_CLICKED, NULL);
  return knob;
}

static void glass_toggle_cb(lv_event_t * e)
{
  lv_obj_t * button = lv_event_get_target(e);
  bool on;

  if(button == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if(cc_panel_drag_claimed) return;

  on = !lv_obj_has_state(button, LV_STATE_CHECKED);
  if(on) lv_obj_add_state(button, LV_STATE_CHECKED);
  else lv_obj_clear_state(button, LV_STATE_CHECKED);

  liquid_glass_button_set_tint(button,
                               on ? lv_color_hex(0x0a84ff) :
                                     lv_color_hex(0xffffff),
                               on ? 140 : 55);
}

static lv_obj_t * cc_round_glass_toggle_create(lv_obj_t * parent,
                                               int32_t x, int32_t y,
                                               int32_t size,
                                               const char * symbol,
                                               bool start_on)
{
  lv_obj_t * button = liquid_glass_button_create(parent, "", size, size);
  lv_obj_t * icon = lv_label_create(button);
  lv_color_t tint = start_on ? lv_color_hex(0x0a84ff) :
                               lv_color_hex(0xffffff);
  lv_opa_t tint_opa = start_on ? 140 : 55;

  lv_obj_set_pos(button, x, y);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_EVENT_BUBBLE);
  cc_button_set_glass_style(button, 14, 10, 3, tint, tint_opa);
  if(start_on) lv_obj_add_state(button, LV_STATE_CHECKED);

  lv_label_set_text(icon, symbol);
  lv_obj_center(icon);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, glass_toggle_cb, LV_EVENT_CLICKED, NULL);
  return button;
}

/**********************
 *   CAPSULE SLIDERS
 **********************/

typedef struct {
  lv_obj_t * fill;
} cc_slider_state_t;

/* Map the pointer position to a fill level between 4% and 100%. */
static void cc_slider_update(lv_obj_t * capsule, cc_slider_state_t * slider,
                             const lv_point_t * point)
{
  lv_area_t coords;
  int32_t usable_h;
  int32_t fill_h;
  int32_t pct;

  lv_obj_get_coords(capsule, &coords);
  usable_h = lv_area_get_height(&coords) - 8;  /* 4 px inset top/bottom */
  if(usable_h <= 0) return;

  pct = ((coords.y2 - 4) - point->y) * 100 / usable_h;
  if(pct < 4) pct = 4;
  if(pct > 100) pct = 100;

  fill_h = usable_h * pct / 100;
  lv_obj_set_height(slider->fill, fill_h);
}

static void cc_slider_event_cb(lv_event_t * e)
{
  cc_slider_state_t * slider = lv_event_get_user_data(e);
  lv_obj_t * capsule = lv_event_get_current_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev;
  lv_point_t point;

  if(slider == NULL || capsule == NULL) return;
  indev = lv_event_get_param(e);
  if(indev == NULL) return;
  lv_indev_get_point(indev, &point);

  if(code == LV_EVENT_PRESSED) {
    slider_press_anim(capsule);
    cc_slider_update(capsule, slider, &point);
  }
  else if(code == LV_EVENT_PRESSING) {
    cc_slider_update(capsule, slider, &point);
  }
  else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    slider_release_anim(capsule);
  }
}

/* Vertical brightness/volume capsule: glass body, frosted fill growing from
 * the bottom, drag anywhere on it to set the level. The jelly stretch
 * animates the whole capsule; the backdrop stays pinned via view scale
 * compensation in the exec callback. */
static lv_obj_t * cc_slider_create(lv_obj_t * parent,
                                   int32_t x, int32_t y,
                                   int32_t w, int32_t h,
                                   int32_t initial_pct)
{
  static cc_slider_state_t slider_states[2];
  static uint32_t slider_count;
  cc_slider_state_t * slider;
  lv_obj_t * capsule;
  lv_obj_t * fill;
  int32_t usable_h = h - 8;

  if(slider_count < 2) slider = &slider_states[slider_count++];
  else return NULL;  /* demo only creates two */

  capsule = cc_tile_create(parent, x, y, w, h, w / 2,
                           &(tile_style_t){
                             .refraction_height = 12,
                             .refraction_amount = 8,
                             .blur = 4,
                             .tint_opa = 45,
                           });
  lv_obj_add_flag(capsule, CC_FLAG_SLIDER);

  fill = lv_obj_create(capsule);
  lv_obj_set_style_align(fill, LV_ALIGN_BOTTOM_MID, 0);
  lv_obj_set_style_translate_y(fill, -4, 0);
  lv_obj_set_width(fill, w - 8);
  lv_obj_set_height(fill, usable_h * initial_pct / 100);
  lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(fill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(fill, 150, 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  slider->fill = fill;

  lv_obj_add_event_cb(capsule, cc_slider_event_cb, LV_EVENT_PRESSED, slider);
  lv_obj_add_event_cb(capsule, cc_slider_event_cb, LV_EVENT_PRESSING, slider);
  lv_obj_add_event_cb(capsule, cc_slider_event_cb, LV_EVENT_RELEASED, slider);
  lv_obj_add_event_cb(capsule, cc_slider_event_cb, LV_EVENT_PRESS_LOST, slider);
  return capsule;
}

/**********************
 *   SHEET (CONTROL CENTER PANEL)
 **********************/

typedef struct {
  lv_obj_t * panel;
  lv_obj_t * content;
  int32_t press_y;
  int32_t start_panel_y;
  int32_t drag_delta;
  bool active;
  bool opening;
} cc_panel_gesture_t;

typedef struct {
  cc_panel_gesture_t * gesture;
  bool opens_panel;
} cc_panel_gesture_source_t;

static cc_panel_gesture_t panel_gesture;
static cc_panel_gesture_source_t open_gesture_source = {
  .gesture = &panel_gesture,
  .opens_panel = true,
};
static cc_panel_gesture_source_t panel_gesture_source = {
  .gesture = &panel_gesture,
  .opens_panel = false,
};

static bool cc_panel_gesture_target_is_slider(lv_obj_t * target,
                                              lv_obj_t * panel)
{
  while(target != NULL && target != panel) {
    if(lv_obj_has_flag(target, CC_FLAG_SLIDER)) return true;
    target = lv_obj_get_parent(target);
  }
  return false;
}

static void cc_panel_gesture_event_cb(lv_event_t * e)
{
  cc_panel_gesture_source_t * source = lv_event_get_user_data(e);
  cc_panel_gesture_t * gesture;
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev = lv_event_get_param(e);
  lv_point_t point;
  int32_t raw_y;

  if(source == NULL || source->gesture == NULL) return;
  gesture = source->gesture;
  if(gesture->panel == NULL || gesture->content == NULL) return;

  if(code == LV_EVENT_PRESSED) {
    int32_t panel_y = lv_obj_get_y(gesture->panel);

    if(indev == NULL) return;
    cc_panel_drag_claimed = false;
    if(demo_settings_is_open()) return;
    if(!source->opens_panel &&
       cc_panel_gesture_target_is_slider(lv_event_get_target_obj(e),
                                         gesture->panel)) return;
    if(source->opens_panel && panel_y >= 0) return;
    if(!source->opens_panel && panel_y <= CC_PANEL_HIDDEN_Y) return;

    lv_anim_delete(gesture->panel, panel_y_exec_cb);
    lv_anim_delete(gesture->content, content_offset_exec_cb);
    lv_indev_get_point(indev, &point);
    gesture->press_y = point.y;
    gesture->start_panel_y = panel_y;
    gesture->drag_delta = 0;
    gesture->opening = source->opens_panel;
    gesture->active = true;
    return;
  }

  if(!gesture->active) return;

  if(code == LV_EVENT_PRESSING) {
    int32_t visual_y;
    int32_t extra_pull;

    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    gesture->drag_delta = point.y - gesture->press_y;
    if(gesture->drag_delta > CC_GESTURE_CLICK_SLOP ||
       gesture->drag_delta < -CC_GESTURE_CLICK_SLOP) {
      cc_panel_drag_claimed = true;
    }
    raw_y = gesture->start_panel_y + gesture->drag_delta;
    visual_y = raw_y;
    extra_pull = 0;

    if(raw_y > 0) {
      visual_y = 0;
      extra_pull = raw_y;
    }
    else if(raw_y < CC_PANEL_HIDDEN_Y) {
      visual_y = CC_PANEL_HIDDEN_Y;
    }

    panel_set_drag_state(gesture->panel, gesture->content,
                         visual_y, extra_pull);
  }
  else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    bool should_open;
    int32_t panel_y = lv_obj_get_y(gesture->panel);
    int32_t target_y;

    if(indev != NULL) {
      lv_indev_get_point(indev, &point);
      gesture->drag_delta = point.y - gesture->press_y;
    }
    should_open = gesture->opening ?
                  (gesture->drag_delta >= CC_PULL_THRESHOLD ||
                   panel_y > -(CC_SCREEN_H / 2)) :
                  (gesture->drag_delta > -CC_PULL_THRESHOLD &&
                   panel_y > -(CC_SCREEN_H / 2));
    gesture->active = false;
    target_y = should_open ? 0 : CC_PANEL_HIDDEN_Y;
    if(panel_y == target_y && cc_content_offset == 0) return;
    panel_snap(gesture->panel, gesture->content, target_y);
  }
}

static void cc_gesture_area_style(lv_obj_t * area)
{
  lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(area, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(area, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(area, 0, 0);
  lv_obj_set_style_pad_all(area, 0, 0);
}

static void cc_open_gesture_area_create(lv_obj_t * screen)
{
  lv_obj_t * zone = lv_obj_create(screen);

  lv_obj_set_pos(zone, 0, 0);
  lv_obj_set_size(zone, CC_SCREEN_W, CC_PULL_EDGE_H);
  cc_gesture_area_style(zone);
  lv_obj_add_event_cb(zone, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESSED, &open_gesture_source);
  lv_obj_add_event_cb(zone, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESSING, &open_gesture_source);
  lv_obj_add_event_cb(zone, cc_panel_gesture_event_cb,
                      LV_EVENT_RELEASED, &open_gesture_source);
  lv_obj_add_event_cb(zone, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESS_LOST, &open_gesture_source);
}

static void home_settings_button_cb(lv_event_t * e)
{
  lv_obj_t * button = lv_event_get_current_target(e);
  lv_area_t coords;

  if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_get_coords(button, &coords);
  demo_settings_open_from((coords.x1 + coords.x2) / 2,
                          (coords.y1 + coords.y2) / 2);
}

static void cc_home_settings_button_create(lv_obj_t * screen)
{
  lv_obj_t * button = liquid_glass_button_create(
    screen, "", CC_HOME_BUTTON_SIZE, CC_HOME_BUTTON_SIZE);
  lv_obj_t * icon = lv_label_create(button);

  lv_obj_set_pos(button,
                 CC_SCREEN_W - CC_HOME_BUTTON_SIZE - 18,
                 CC_SCREEN_H - CC_HOME_BUTTON_SIZE - 18);
  cc_button_set_glass_style(button, 14, 10, 3,
                            lv_color_hex(0xffffff), 70);
  lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
  lv_obj_center(icon);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
  lv_obj_add_event_cb(button, home_settings_button_cb, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t * cc_panel_create(lv_obj_t * parent)
{
  lv_obj_t * panel = lv_obj_create(parent);
  lv_obj_t * content;
  liquid_glass_interactive_style_t card_style = {
    .refraction_height = 16,
    .refraction_amount = 12.0f,
    .dispersion = 0.3f,
    .blur_radius = 5,
    .tint_opa = 50,
  };
  lv_obj_t * conn;
  lv_obj_t * media;

  lv_obj_set_size(panel, CC_SCREEN_W, CC_SCREEN_H);
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(panel, LV_DIR_NONE);
  lv_obj_set_style_radius(panel, 40, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_clip_corner(panel, true, 0);
  panel_gesture.panel = panel;
  lv_obj_add_event_cb(panel, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESSED, &panel_gesture_source);
  lv_obj_add_event_cb(panel, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESSING, &panel_gesture_source);
  lv_obj_add_event_cb(panel, cc_panel_gesture_event_cb,
                      LV_EVENT_RELEASED, &panel_gesture_source);
  lv_obj_add_event_cb(panel, cc_panel_gesture_event_cb,
                      LV_EVENT_PRESS_LOST, &panel_gesture_source);

  /* The control center is one big glass surface with nested controls. */
  liquid_glass_apply(panel, NULL);
  {
    liquid_glass_params_t * params = liquid_glass_get_params(panel);

    if(params != NULL) params->corner_radius = 40;
  }
  liquid_glass_set_blur(panel, 10);
  liquid_glass_set_tint(panel, lv_color_hex(0x0a1a2f), 60);

  content = lv_obj_create(panel);
  lv_obj_set_pos(content, 0, 0);
  lv_obj_set_size(content, CC_SCREEN_W, CC_SCREEN_H);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK |
                          LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  panel_gesture.content = content;

  card_style.tint_color = lv_color_hex(0xffffff);

  /* --- connectivity tile: interactive glass with standard buttons --- */
  conn = liquid_glass_interactive_create(content, 140, 140, CC_TILE_R);
  if(conn != NULL) {
    lv_obj_set_pos(conn, 14, 64);
    liquid_glass_interactive_set_style(conn, &card_style);
    cc_round_toggle_create(conn, 14, 14, 48, LV_SYMBOL_WIFI, true);
    cc_round_toggle_create(conn, 78, 14, 48, LV_SYMBOL_BLUETOOTH, true);
    cc_round_toggle_create(conn, 14, 78, 48, LV_SYMBOL_GPS, false);
  }

  /* --- media tile --- */
  media = liquid_glass_interactive_create(content, 140, 140, CC_TILE_R);
  if(media != NULL) {
    lv_obj_t * media_icon = lv_label_create(media);
    lv_obj_t * media_title = lv_label_create(media);
    lv_obj_t * media_bar = lv_bar_create(media);

    lv_obj_set_pos(media, 166, 64);
    liquid_glass_interactive_set_style(media, &card_style);

    lv_label_set_text(media_icon, LV_SYMBOL_AUDIO);
    lv_obj_align(media_icon, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_text_color(media_icon, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(media_icon, &lv_font_montserrat_24, 0);

    lv_label_set_text(media_title, "Liquid FM");
    lv_obj_align(media_title, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_color(media_title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(media_title, 210, 0);

    lv_obj_set_size(media_bar, 108, 6);
    lv_obj_align(media_bar, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_clear_flag(media_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_bar_set_range(media_bar, 0, 100);
    lv_bar_set_value(media_bar, 62, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(media_bar, lv_color_hex(0xffffff),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(media_bar, 60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(media_bar, lv_color_hex(0xffffff),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(media_bar, 220, LV_PART_INDICATOR);
    lv_obj_set_style_radius(media_bar, 3, LV_PART_MAIN);
  }

  /* --- standalone round glass buttons --- */
  cc_round_glass_toggle_create(content, 14, 224, 64, LV_SYMBOL_BELL, false);
  cc_round_glass_toggle_create(content, 90, 224, 64, LV_SYMBOL_CHARGE, false);

  /* --- wide focus capsule --- */
  lv_obj_t * focus = liquid_glass_button_create(content, "", 140, 64);
  lv_obj_t * focus_icon = lv_label_create(focus);
  lv_obj_t * focus_label = lv_label_create(focus);

  lv_obj_set_pos(focus, 14, 300);
  lv_obj_add_flag(focus, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_EVENT_BUBBLE);
  cc_button_set_glass_style(focus, 14, 10, 3,
                            lv_color_hex(0xffffff), 55);
  lv_label_set_text(focus_icon, LV_SYMBOL_EYE_OPEN);
  lv_obj_align(focus_icon, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_set_style_text_color(focus_icon, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(focus_icon, &lv_font_montserrat_18, 0);
  lv_label_set_text(focus_label, "Focus");
  lv_obj_align(focus_label, LV_ALIGN_LEFT_MID, 48, 0);
  lv_obj_set_style_text_color(focus_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(focus_label, &lv_font_montserrat_16, 0);
  lv_obj_add_event_cb(focus, glass_toggle_cb, LV_EVENT_CLICKED, NULL);

  /* --- brightness + volume capsules --- */
  cc_slider_create(content, 166, CC_SLIDER_Y, 64, CC_SLIDER_H, 72);
  cc_slider_create(content, 242, CC_SLIDER_Y, 64, CC_SLIDER_H, 45);
  lv_obj_set_y(panel, CC_PANEL_HIDDEN_Y);
  liquid_glass_mark_dirty(panel);

  return panel;
}

static void create_wallpaper(lv_obj_t * screen)
{
  lv_obj_t * background = lv_image_create(screen);

  lv_image_set_src(background, &img);
  lv_obj_center(background);
  lv_obj_move_background(background);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void demo_control_center_run(void)
{
  lv_obj_t * screen = lv_screen_active();

  /* Refresh dirty glass at display cadence so the button lens tracks input. */
  /* GPU (LIQUID_GLASS_RENDER_AUTO / LIQUID_GLASS_RENDER_SHADER) can be
   * re-enabled here once the desired look is tuned on the CPU path. */
  liquid_glass_set_render_mode(LIQUID_GLASS_RENDER_CPU);
  liquid_glass_set_refresh_policy(LIQUID_GLASS_REFRESH_DIRTY);
  liquid_glass_set_refresh_period(16);
  panel_gesture = (cc_panel_gesture_t){0};
  cc_panel_drag_claimed = false;
  cc_content_offset = 0;

  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(screen, LV_DIR_NONE);

  create_wallpaper(screen);
  cc_home_settings_button_create(screen);
  cc_open_gesture_area_create(screen);
  cc_panel_create(screen);
  demo_settings_init();
}
