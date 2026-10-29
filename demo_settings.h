#ifndef DEMO_SETTINGS_H
#define DEMO_SETTINGS_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Android-style Settings app: a frosted glass sheet that opens from
 *  the bottom. Its sliders drive the global liquid glass overrides, so
 *  dragging them live-updates every glass surface on the screen (including
 *  the sheet itself). Create it with demo_settings_init() after the
 *  underlying screen so it stacks on top. */
void demo_settings_init(void);

/** Open with the classic launcher transition: the sheet grows out of the
 *  given point (screen coordinates, e.g. the tapped app icon's center),
 *  zooming in while fading in. */
void demo_settings_open_from(int32_t origin_x, int32_t origin_y);

/** Open from the bottom edge. */
void demo_settings_open(void);

/** Zoom back into the point the sheet was launched from, then hide. */
void demo_settings_close(void);
void demo_settings_toggle(void);
bool demo_settings_is_open(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
