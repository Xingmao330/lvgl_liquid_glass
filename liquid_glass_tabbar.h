#ifndef LIQUID_GLASS_TABBAR_H
#define LIQUID_GLASS_TABBAR_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a liquid glass tab bar. The tab labels are copied by LVGL when
 * assigned, so the caller may release the labels array after this returns.
 * Listen for LV_EVENT_VALUE_CHANGED on the returned object when the active
 * tab changes.
 */
lv_obj_t * liquid_glass_tabbar_create(lv_obj_t * parent,
                                      const char * const labels[],
                                      uint32_t tab_count,
                                      int32_t width,
                                      int32_t height);

void liquid_glass_tabbar_set_active(lv_obj_t * tabbar,
                                    uint32_t index,
                                    lv_anim_enable_t anim_en);
uint32_t liquid_glass_tabbar_get_active(const lv_obj_t * tabbar);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
