/**
 * @file demo_settings.c
 *
 * Android-style Settings app: a frosted glass sheet that opens from the
 * bottom of the screen. Its sliders drive the global liquid glass overrides
 * (liquid_glass_set_global_*), so dragging them live-updates every glass
 * surface on screen - including this sheet itself, which is glass too.
 */

#include "demo_settings.h"
#include "liquid_glass_widget.h"
#include "lvgl/lvgl.h"

#define SETTINGS_W        320
#define SETTINGS_H        439
#define SETTINGS_SCREEN_H  483
#define SETTINGS_OPEN_Y    (SETTINGS_SCREEN_H - SETTINGS_H)
#define SETTINGS_ROW_H    62
#define SETTINGS_ROW_X    16
#define SETTINGS_ROW_W    288

typedef enum {
  ROW_BLUR,
  ROW_REFRACTION,
  ROW_AMOUNT,
  ROW_DISPERSION,
  ROW_TINT,
} row_type_t;

typedef struct {
  row_type_t type;
  lv_obj_t * value_label;
} row_state_t;

static lv_obj_t * sheet;
static bool opened;
static row_state_t rows[5];

/* Launcher-style zoom transition: the sheet grows out of / shrinks back
 * into the point it was launched from. */
#define ZOOM_MIN_SCALE   40     /* ~16% at launch */
static int32_t open_origin_x = SETTINGS_W / 2;
static int32_t open_origin_y = SETTINGS_H;

static void close_btn_cb(lv_event_t * e)
{
  (void)e;
  demo_settings_close();
}

static void zoom_anim_exec_cb(void * var, int32_t v)
{
  lv_obj_t * s = var;

  lv_obj_set_style_transform_scale(s, v, 0);
  lv_obj_set_style_opa(s, (lv_opa_t)(v * 255 / LV_SCALE_NONE), 0);
}

static void zoom_in_ready_cb(lv_anim_t * a)
{
  lv_obj_t * s = (lv_obj_t *)a->var;

  lv_obj_set_style_transform_scale(s, LV_SCALE_NONE, 0);
  lv_obj_set_style_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_transform_pivot_x(s, SETTINGS_W / 2, 0);
  lv_obj_set_style_transform_pivot_y(s, SETTINGS_H / 2, 0);
  liquid_glass_mark_dirty(s);
}

static void zoom_out_ready_cb(lv_anim_t * a)
{
  lv_obj_t * s = (lv_obj_t *)a->var;

  lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_transform_scale(s, LV_SCALE_NONE, 0);
  lv_obj_set_style_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_transform_pivot_x(s, SETTINGS_W / 2, 0);
  lv_obj_set_style_transform_pivot_y(s, SETTINGS_H / 2, 0);
}

static void zoom_to(int32_t target_scale, bool hide_on_ready)
{
  lv_anim_t anim;

  lv_anim_init(&anim);
  lv_anim_set_var(&anim, sheet);
  lv_anim_set_exec_cb(&anim, zoom_anim_exec_cb);
  lv_anim_set_values(&anim,
                     lv_obj_get_style_transform_scale_x(sheet, 0),
                     target_scale);
  lv_anim_set_duration(&anim, 360);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&anim,
                           hide_on_ready ? zoom_out_ready_cb : zoom_in_ready_cb);
  lv_anim_start(&anim);

  liquid_glass_mark_dirty(sheet);
}

static void realtime_switch_cb(lv_event_t * e)
{
  lv_obj_t * sw = lv_event_get_target(e);

  liquid_glass_set_refresh_policy(
    lv_obj_has_state(sw, LV_STATE_CHECKED) ?
    LIQUID_GLASS_REFRESH_ALWAYS : LIQUID_GLASS_REFRESH_DIRTY);
}

static void slider_event_cb(lv_event_t * e)
{
  row_state_t * row = lv_event_get_user_data(e);
  lv_obj_t * slider = lv_event_get_target(e);
  int32_t v = lv_slider_get_value(slider);
  char text[16];

  switch(row->type) {
    case ROW_BLUR:
      liquid_glass_set_global_blur(v);
      lv_snprintf(text, sizeof(text), "%d px", v);
      break;
    case ROW_REFRACTION:
      liquid_glass_set_global_refraction(v, -1);
      lv_snprintf(text, sizeof(text), "%d px", v);
      break;
    case ROW_AMOUNT:
      liquid_glass_set_global_refraction(-1, v * 10);
      lv_snprintf(text, sizeof(text), "%d", v);
      break;
    case ROW_DISPERSION:
      liquid_glass_set_global_dispersion(v * 10);
      lv_snprintf(text, sizeof(text), "%d.%d", v / 10, v % 10);
      break;
    case ROW_TINT:
      liquid_glass_set_global_tint_opa(v);
      lv_snprintf(text, sizeof(text), "%d", v);
      break;
    default:
      text[0] = '\0';
      break;
  }

  if(row->value_label != NULL) lv_label_set_text(row->value_label, text);
}

/* Title + value label + full-width slider. */
static void add_slider_row(lv_obj_t * parent, int32_t y,
                           const char * title, const char * value_text,
                           int32_t min, int32_t max, int32_t value,
                           row_state_t * row)
{
  lv_obj_t * label = lv_label_create(parent);
  lv_obj_t * value_label = lv_label_create(parent);
  lv_obj_t * slider = lv_slider_create(parent);

  lv_label_set_text(label, title);
  lv_obj_set_pos(label, SETTINGS_ROW_X, y + 8);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);

  lv_label_set_text(value_label, value_text);
  lv_obj_set_pos(value_label, SETTINGS_ROW_X + SETTINGS_ROW_W - 60, y + 8);
  lv_obj_set_width(value_label, 60);
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(value_label, lv_color_hex(0xbfe3ff), 0);

  lv_obj_set_size(slider, SETTINGS_ROW_W, 12);
  lv_obj_set_pos(slider, SETTINGS_ROW_X, y + 34);
  lv_slider_set_range(slider, min, max);
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, 60, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, 200, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, 255, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(slider, 6, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(slider, 70, LV_PART_KNOB);

  row->value_label = value_label;
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, row);
}

static void add_separator(lv_obj_t * parent, int32_t y)
{
  lv_obj_t * sep = lv_obj_create(parent);

  lv_obj_set_size(sep, SETTINGS_ROW_W, 1);
  lv_obj_set_pos(sep, SETTINGS_ROW_X, y);
  lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(sep, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(sep, 28, 0);
  lv_obj_set_style_border_width(sep, 0, 0);
}

static void create_sheet(void)
{
  liquid_glass_params_t defaults;
  int32_t blur = liquid_glass_get_global_blur();
  int32_t refr_h = liquid_glass_get_global_refraction_height();
  int32_t refr_a = liquid_glass_get_global_refraction_amount_x10();
  int32_t disp = liquid_glass_get_global_dispersion_x10();
  int32_t tint = liquid_glass_get_global_tint_opa();
  char text[16];
  int32_t y;
  lv_obj_t * title;
  lv_obj_t * close_label;
  lv_obj_t * grabber;
  lv_obj_t * sw_label;
  lv_obj_t * sw;

  sheet = lv_obj_create(lv_screen_active());
  lv_obj_set_size(sheet, SETTINGS_W, SETTINGS_H);
  lv_obj_set_pos(sheet, 0, SETTINGS_SCREEN_H);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(sheet, LV_DIR_NONE);
  lv_obj_set_style_radius(sheet, 24, 0);
  lv_obj_set_style_pad_all(sheet, 0, 0);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sheet, 0, 0);
  lv_obj_set_style_clip_corner(sheet, true, 0);
  lv_obj_set_style_transform_pivot_x(sheet, SETTINGS_W / 2, 0);
  lv_obj_set_style_transform_pivot_y(sheet, SETTINGS_H / 2, 0);

  liquid_glass_apply(sheet, NULL);
  liquid_glass_set_blur(sheet, 8);
  liquid_glass_set_tint(sheet, lv_color_hex(0x0a1a2f), 70);

  grabber = lv_obj_create(sheet);
  lv_obj_set_size(grabber, 96, 4);
  lv_obj_align(grabber, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_clear_flag(grabber, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(grabber, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(grabber, 2, 0);
  lv_obj_set_style_bg_color(grabber, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(grabber, 110, 0);
  lv_obj_set_style_border_width(grabber, 0, 0);

  title = lv_label_create(sheet);
  lv_label_set_text(title, "Settings");
  lv_obj_set_pos(title, 16, 16);
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  close_label = lv_label_create(sheet);
  lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
  lv_obj_set_pos(close_label, SETTINGS_W - 40, 16);
  lv_obj_set_style_text_color(close_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(close_label, &lv_font_montserrat_20, 0);
  lv_obj_add_flag(close_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(close_label, close_btn_cb, LV_EVENT_CLICKED, NULL);

  /* Defaults for the sliders while no global override is set yet. */
  liquid_glass_params_init(&defaults);
  if(blur < 0) blur = 5;
  if(refr_h < 0) refr_h = defaults.refraction_height;
  if(refr_a < 0) refr_a = (int32_t)(defaults.refraction_amount * 10.0f);
  if(disp < 0) disp = (int32_t)(defaults.dispersion * 10.0f);
  if(tint < 0) tint = 60;

  rows[ROW_BLUR].type = ROW_BLUR;
  rows[ROW_REFRACTION].type = ROW_REFRACTION;
  rows[ROW_AMOUNT].type = ROW_AMOUNT;
  rows[ROW_DISPERSION].type = ROW_DISPERSION;
  rows[ROW_TINT].type = ROW_TINT;

  y = 56;
  lv_snprintf(text, sizeof(text), "%d px", blur);
  add_slider_row(sheet, y, "Background blur", text, 0, 12, blur,
                 &rows[ROW_BLUR]);
  y += SETTINGS_ROW_H;
  add_separator(sheet, y - 8);
  lv_snprintf(text, sizeof(text), "%d px", refr_h);
  add_slider_row(sheet, y, "Refraction height", text, 0, 60, refr_h,
                 &rows[ROW_REFRACTION]);
  y += SETTINGS_ROW_H;
  add_separator(sheet, y - 8);
  lv_snprintf(text, sizeof(text), "%d", refr_a / 10);
  add_slider_row(sheet, y, "Refraction amount", text, 0, 60, refr_a / 10,
                 &rows[ROW_AMOUNT]);
  y += SETTINGS_ROW_H;
  add_separator(sheet, y - 8);
  lv_snprintf(text, sizeof(text), "%d.%d", disp / 10, disp % 10);
  add_slider_row(sheet, y, "Dispersion", text, 0, 30, disp / 10,
                 &rows[ROW_DISPERSION]);
  y += SETTINGS_ROW_H;
  add_separator(sheet, y - 8);
  lv_snprintf(text, sizeof(text), "%d", tint);
  add_slider_row(sheet, y, "Tint opacity", text, 0, 200, tint,
                 &rows[ROW_TINT]);
  y += SETTINGS_ROW_H;
  add_separator(sheet, y - 8);

  /* Realtime refresh toggle. */
  sw_label = lv_label_create(sheet);
  lv_label_set_text(sw_label, "Realtime refresh");
  lv_obj_set_pos(sw_label, SETTINGS_ROW_X, y + 14);
  lv_obj_set_style_text_color(sw_label, lv_color_hex(0xffffff), 0);

  sw = lv_switch_create(sheet);
  lv_obj_set_size(sw, 52, 26);
  lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -SETTINGS_ROW_X, y + 4);
  if(liquid_glass_get_refresh_policy() == LIQUID_GLASS_REFRESH_ALWAYS) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, realtime_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void demo_settings_init(void)
{
  if(sheet == NULL) create_sheet();
}

void demo_settings_open_from(int32_t origin_x, int32_t origin_y)
{
  lv_area_t sheet_area;

  if(sheet == NULL || opened) return;

  lv_anim_delete(sheet, zoom_anim_exec_cb);

  lv_obj_set_pos(sheet, 0, SETTINGS_OPEN_Y);
  lv_obj_get_coords(sheet, &sheet_area);
  open_origin_x = origin_x - sheet_area.x1;
  open_origin_y = origin_y - sheet_area.y1;
  lv_obj_clear_flag(sheet, LV_OBJ_FLAG_HIDDEN);
  /* Grow out of the launch point: pivot there, start small and faded. */
  lv_obj_set_style_transform_pivot_x(sheet, open_origin_x, 0);
  lv_obj_set_style_transform_pivot_y(sheet, open_origin_y, 0);
  lv_obj_set_style_transform_scale(sheet, ZOOM_MIN_SCALE, 0);
  lv_obj_set_style_opa(sheet, ZOOM_MIN_SCALE * 255 / LV_SCALE_NONE, 0);

  opened = true;
  zoom_to(LV_SCALE_NONE, false);
}

void demo_settings_open(void)
{
  demo_settings_open_from(SETTINGS_W / 2, SETTINGS_SCREEN_H);
}

void demo_settings_close(void)
{
  if(sheet == NULL || !opened) return;

  lv_anim_delete(sheet, zoom_anim_exec_cb);
  opened = false;

  lv_obj_set_style_transform_pivot_x(sheet, open_origin_x, 0);
  lv_obj_set_style_transform_pivot_y(sheet, open_origin_y, 0);
  zoom_to(ZOOM_MIN_SCALE, true);
}

void demo_settings_toggle(void)
{
  if(opened) demo_settings_close();
  else demo_settings_open();
}

bool demo_settings_is_open(void)
{
  return opened;
}
