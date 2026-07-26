#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0
#define LV_MEM_SIZE        (128U * 1024U)

#define LV_TICK_CUSTOM     0
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_USE_LABEL       1
#define LV_USE_BTN         1
#define LV_USE_LIST        1
#define LV_USE_ARC         1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_20  1


#define LV_USE_LOG         1
#if LV_USE_LOG
  #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF 0
#endif
#define LV_USE_ASSERT_NULL 1

#endif
