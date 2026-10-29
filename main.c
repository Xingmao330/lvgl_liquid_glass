/**
 * @file main.c
 *
 * LVGL + SDL entry point. LVGL is initialized here; the actual UI is one of
 * the liquid glass demos selected by DEMO_TO_RUN:
 *   0 - demo_control_center: iOS-style control center (default)
 *   1 - demo_liquid_glass:   draggable parameter panel + glass button
 *   2 - demo_liquid_glass_button: interactive glass button control
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE /* needed for usleep() */
#endif

#include <stdlib.h>
#include <stdio.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
#endif
#include "lvgl/lvgl.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"
#include <SDL.h>

#include "hal/hal.h"
#include "liquid_glass_widget.h"
#include "demo_control_center.h"
#include "demo_liquid_glass.h"
#include "demo_liquid_glass_button.h"


int main(int argc, char **argv)
{
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(320, 483);

  demo_control_center_run();

  //demo_liquid_glass_run();

  //demo_liquid_glass_button_run();


  /* The demos pick the render backend on init; report the result. */
  printf("Liquid glass renderer: %s\n",
         liquid_glass_using_shader() ? "desktop OpenGL shader" : "CPU fallback");

  while(1) {
    /* Periodically call the lv_task handler.
     * It could be done in a timer interrupt or an OS task too.*/
    uint32_t sleep_time_ms = lv_timer_handler();
    if(sleep_time_ms == LV_NO_TIMER_READY){
      sleep_time_ms =  LV_DEF_REFR_PERIOD;
    }
    /* Closing the window deletes the display; stop instead of letting the
     * remaining processing warn about orphaned objects forever. */
    if(lv_display_get_default() == NULL){
      break;
    }
#ifdef _MSC_VER
    Sleep(sleep_time_ms);
#else
    usleep(sleep_time_ms * 1000);
#endif
  }

  return 0;
}
