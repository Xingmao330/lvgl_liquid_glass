#include "demo_liquid_glass_button.h"

#include "liquid_glass_button.h"
#include "liquid_glass_tabbar.h"
#include "liquid_glass_widget.h"
#include "lvgl/lvgl.h"

LV_IMAGE_DECLARE(img);

static uint32_t button_press_count;
static const char * const demo_tab_names[] = {
  "HOME", "GLASS", "MOTION", "INFO"
};

static lv_obj_t * create_marker(lv_obj_t * parent,
                                int32_t x,
                                int32_t y,
                                int32_t size,
                                uint32_t color)
{
  lv_obj_t * marker = lv_obj_create(parent);

  lv_obj_set_size(marker, size, size);
  lv_obj_set_pos(marker, x, y);
  lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(marker, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(marker, 0, 0);
  lv_obj_set_style_shadow_color(marker, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(marker, 14, 0);
  lv_obj_set_style_shadow_opa(marker, 90, 0);
  return marker;
}

static void button_clicked_cb(lv_event_t * e)
{
  lv_obj_t * count_label = lv_event_get_user_data(e);

  button_press_count++;
  lv_label_set_text_fmt(count_label,
                        "BUTTON ACTIVATED  /  %lu",
                        (unsigned long)button_press_count);
}

static void tab_changed_cb(lv_event_t * e)
{
  static const char * const tab_names[] = {
    "HOME", "GLASS", "MOTION", "INFO"
  };
  lv_obj_t * status_label = lv_event_get_user_data(e);
  lv_obj_t * tabbar = lv_event_get_target(e);
  uint32_t active = liquid_glass_tabbar_get_active(tabbar);

  if(status_label == NULL ||
     active >= sizeof(tab_names) / sizeof(tab_names[0])) return;
  lv_label_set_text_fmt(status_label, "ACTIVE TAB  /  %s", tab_names[active]);
}

void demo_liquid_glass_button_run(void)
{
  lv_obj_t * screen = lv_screen_active();
  lv_obj_t * background;
  lv_obj_t * eyebrow;
  lv_obj_t * title;
  lv_obj_t * subtitle;
  lv_obj_t * backdrop_label;
  lv_obj_t * tab_status_label;
  lv_obj_t * count_label;
  lv_obj_t * instruction;
  lv_obj_t * button;
  lv_obj_t * tabbar;

  button_press_count = 0;
  liquid_glass_set_render_mode(LIQUID_GLASS_RENDER_CPU);
  liquid_glass_set_refresh_policy(LIQUID_GLASS_REFRESH_ALWAYS);
  liquid_glass_set_refresh_period(16);

  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(screen, LV_DIR_NONE);

  background = lv_image_create(screen);
  lv_image_set_src(background, &img);
  lv_obj_center(background);
  lv_obj_move_background(background);

  create_marker(screen, 22, 142, 18, 0xf5c451);
  create_marker(screen, 270, 164, 28, 0xf06c87);
  create_marker(screen, 50, 314, 26, 0x61e3cb);
  create_marker(screen, 250, 326, 16, 0x9ca7ff);

  eyebrow = lv_label_create(screen);
  lv_label_set_text(eyebrow, "LVGL  /  INTERACTIVE CONTROL");
  lv_obj_align(eyebrow, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_text_color(eyebrow, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(eyebrow, 205, 0);
  lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_12, 0);

  title = lv_label_create(screen);
  lv_label_set_text(title, "Liquid Glass");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 55);
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

  subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "Spring, deformation and touch response");
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 94);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(subtitle, 220, 0);
  lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);

  tabbar = liquid_glass_tabbar_create(screen, demo_tab_names,
                                      sizeof(demo_tab_names) /
                                        sizeof(demo_tab_names[0]),
                                      284, 64);
  if(tabbar != NULL) lv_obj_align(tabbar, LV_ALIGN_TOP_MID, 0, 125);

  tab_status_label = lv_label_create(screen);
  lv_label_set_text(tab_status_label, "ACTIVE TAB  /  HOME");
  lv_obj_align(tab_status_label, LV_ALIGN_TOP_MID, 0, 198);
  lv_obj_set_style_text_color(tab_status_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(tab_status_label, 235, 0);
  lv_obj_set_style_text_font(tab_status_label, &lv_font_montserrat_12, 0);
  if(tabbar != NULL) {
    lv_obj_add_event_cb(tabbar, tab_changed_cb, LV_EVENT_VALUE_CHANGED,
                        tab_status_label);
  }

  backdrop_label = lv_label_create(screen);
  lv_label_set_text(backdrop_label, "TOUCH  /  DRAG  /  RELEASE");
  lv_obj_align(backdrop_label, LV_ALIGN_TOP_MID, 0, 222);
  lv_obj_set_style_text_color(backdrop_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(backdrop_label, 230, 0);
  lv_obj_set_style_text_font(backdrop_label, &lv_font_montserrat_16, 0);

  button = liquid_glass_button_create(screen, "PRESS AND MOVE", 232, 68);
  lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 253);

  count_label = lv_label_create(screen);
  lv_label_set_text(count_label, "READY  /  0");
  lv_obj_align(count_label, LV_ALIGN_TOP_MID, 0, 343);
  lv_obj_set_style_text_color(count_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(count_label, &lv_font_montserrat_14, 0);
  lv_obj_add_event_cb(button, button_clicked_cb, LV_EVENT_CLICKED, count_label);

  instruction = lv_label_create(screen);
  lv_label_set_text(instruction, "The lens follows your finger and springs back");
  lv_obj_align(instruction, LV_ALIGN_TOP_MID, 0, 379);
  lv_obj_set_style_text_color(instruction, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_opa(instruction, 220, 0);
  lv_obj_set_style_text_font(instruction, &lv_font_montserrat_12, 0);
}
