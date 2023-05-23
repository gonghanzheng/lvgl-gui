/**
 * @file lv_color.h
 *
 */

#ifndef LV_COLOR_H
#define LV_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lv_conf_internal.h"
#include "lv_assert.h"
#include "lv_math.h"
#include "lv_types.h"
#include <stdint.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
LV_EXPORT_CONST_INT(LV_COLOR_DEPTH);

/**
 * Opacity percentages.
 */

enum _lv_opa_t {
    LV_OPA_TRANSP = 0,
    LV_OPA_0      = 0,
    LV_OPA_10     = 25,
    LV_OPA_20     = 51,
    LV_OPA_30     = 76,
    LV_OPA_40     = 102,
    LV_OPA_50     = 127,
    LV_OPA_60     = 153,
    LV_OPA_70     = 178,
    LV_OPA_80     = 204,
    LV_OPA_90     = 229,
    LV_OPA_100    = 255,
    LV_OPA_COVER  = 255,
};

#ifdef DOXYGEN
typedef _lv_opa_t lv_opa_t;
#else
typedef uint8_t lv_opa_t;
#endif /*DOXYGEN*/


#define LV_OPA_MIN 2    /*Opacities below this will be transparent*/
#define LV_OPA_MAX 253  /*Opacities above this will fully cover*/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} lv_color_t;

typedef struct {
    uint16_t blue : 5;
    uint16_t green : 6;
    uint16_t red : 5;
} lv_color16_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} lv_color32_t;

typedef struct {
    uint16_t h;
    uint8_t s;
    uint8_t v;
} lv_color_hsv_t;

//! @cond Doxygen_Suppress
/*No idea where the guard is required but else throws warnings in the docs*/
typedef uint8_t lv_opa_t;
//! @endcond


typedef enum {
    LV_COLOR_FORMAT_UNKNOWN,

    /*<=1 byte (+alpha) formats*/
    LV_COLOR_FORMAT_L8,
    LV_COLOR_FORMAT_A8,
    LV_COLOR_FORMAT_I1,
    LV_COLOR_FORMAT_I2,
    LV_COLOR_FORMAT_I4,
    LV_COLOR_FORMAT_I8,

    /*2 byte (+alpha) formats*/
    LV_COLOR_FORMAT_RGB565,
    LV_COLOR_FORMAT_RGB565_CHROMA_KEYED,
    LV_COLOR_FORMAT_RGB565A8,       /**< Color array followed by Alpha array*/

    /*3 byte (+alpha) formats*/
    LV_COLOR_FORMAT_RGB888,
    LV_COLOR_FORMAT_RGB888_CHROMA_KEYED,
    LV_COLOR_FORMAT_ARGB8888,
    LV_COLOR_FORMAT_XRGB8888,

    /*Color formats in which LVGL can render*/
#if LV_COLOR_DEPTH == 8
    LV_COLOR_FORMAT_NATIVE = LV_COLOR_FORMAT_L8,
#elif LV_COLOR_DEPTH == 16
    LV_COLOR_FORMAT_NATIVE =  LV_COLOR_FORMAT_RGB565,
#elif LV_COLOR_DEPTH == 24
    LV_COLOR_FORMAT_NATIVE = LV_COLOR_FORMAT_RGB888,
#elif LV_COLOR_DEPTH == 32
    LV_COLOR_FORMAT_NATIVE = LV_COLOR_FORMAT_XRGB8888,
#endif
    /*Miscellaneous formats*/
    LV_COLOR_FORMAT_NATIVE_REVERSED = 0x1A,

    LV_COLOR_FORMAT_RAW,
    LV_COLOR_FORMAT_RAW_ALPHA,
} lv_color_format_t;


/**********************
 * MACROS
 **********************/

# define LV_COLOR_MAKE(r8, g8, b8) {b8, g8, r8}

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Get the pixel size of a color format in bits
 * @param src_cf a color format (`LV_IMG_CF_...`)
 * @return the pixel size in bits
 */
uint8_t lv_color_format_get_size(lv_color_format_t src_cf);

/**
 * Check if a color format has alpha channel or not
 * @param src_cf a color format (`LV_IMG_CF_...`)
 * @return true: has alpha channel; false: doesn't have alpha channel
 */
bool lv_color_format_has_alpha(lv_color_format_t src_cf);


lv_color32_t lv_color_to_32(lv_color_t color, lv_opa_t opa);

static inline uint32_t lv_color_to_int(lv_color_t c)
{
    uint8_t * tmp = (uint8_t *) &c;
    return tmp[0] + (tmp[1] << 8) + (tmp[2] << 16);
}


static inline lv_color_t lv_color_from_int(uint16_t v)
{
    return *((lv_color_t *)((uint8_t *)&v));
}

static inline bool lv_color_eq(lv_color_t c1, lv_color_t c2)
{
    return lv_color_to_int(c1) == lv_color_to_int(c2);
}

static inline bool lv_color32_eq(lv_color32_t c1, lv_color32_t c2)
{
    return *((uint32_t *)&c1) == *((uint32_t *)&c2);
}

static lv_color_t lv_color_hex(uint32_t c)
{
    lv_color_t ret;
    ret.red = (c >> 16) & 0xff;
    ret.green = (c >> 8) & 0xff;
    ret.blue = (c >> 0) & 0xff;
    return ret;
}

static inline lv_color_t lv_color_make(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_hex((r << 16) + (g << 8) + b);
}


static inline lv_color_t lv_color_hex3(uint32_t c)
{
    return lv_color_make((uint8_t)(((c >> 4) & 0xF0) | ((c >> 8) & 0xF)), (uint8_t)((c & 0xF0) | ((c & 0xF0) >> 4)),
                         (uint8_t)((c & 0xF) | ((c & 0xF) << 4)));
}

/**
 * Just a wrapper around LV_COLOR_CHROMA_KEY because it might be more convenient to use a function in some cases
 * @return LV_COLOR_CHROMA_KEY
 */
static inline lv_color_t lv_color_chroma_key(void)
{
    return LV_COLOR_CHROMA_KEY;
}


uint16_t lv_color_to_u16(lv_color_t color);
uint32_t lv_color_to_u32(lv_color_t color);

lv_color_t lv_color_lighten(lv_color_t c, lv_opa_t lvl);

lv_color_t lv_color_darken(lv_color_t c, lv_opa_t lvl);

/**
 * Convert a HSV color to RGB
 * @param h hue [0..359]
 * @param s saturation [0..100]
 * @param v value [0..100]
 * @return the given RGB color in RGB (with LV_COLOR_DEPTH depth)
 */
lv_color_t lv_color_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v);

/**
 * Convert a 32-bit RGB color to HSV
 * @param r8 8-bit red
 * @param g8 8-bit green
 * @param b8 8-bit blue
 * @return the given RGB color in HSV
 */
lv_color_hsv_t lv_color_rgb_to_hsv(uint8_t r8, uint8_t g8, uint8_t b8);

/**
 * Convert a color to HSV
 * @param color color
 * @return the given color in HSV
 */
lv_color_hsv_t lv_color_to_hsv(lv_color_t color);

/*Source: https://vuetifyjs.com/en/styles/colors/#material-colors*/

static inline lv_color_t lv_color_white(void)
{
    return lv_color_make(0xff, 0xff, 0xff);
}
static inline lv_color_t lv_color_black(void)
{
    return lv_color_make(0x00, 0x00, 0x00);
}

/**********************
 *      MACROS
 **********************/

#include "lv_palette.h"
#include "lv_color_op.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_COLOR_H*/
