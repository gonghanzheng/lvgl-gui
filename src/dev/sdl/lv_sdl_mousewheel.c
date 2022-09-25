/**
 * @file lv_sdl_mousewheel.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_sdl_mousewheel.h"
#if LV_USE_SDL

#include "../../core/lv_group.h"
#include LV_SDL_INCLUDE_PATH

/*********************
 *      DEFINES
 *********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void sdl_mousewheel_read(lv_indev_t * indev, lv_indev_data_t * data);

/**********************
 *  STATIC VARIABLES
 **********************/

typedef struct {
    int16_t diff;
    lv_indev_state_t state;
} lv_sdl_mousewheel_t;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_indev_t * lv_sdl_mousewheel_create(void)
{
    lv_sdl_mousewheel_t * dsc = lv_malloc(sizeof(lv_sdl_mousewheel_t));
    LV_ASSERT_MALLOC(dsc);
    if(dsc == NULL) return NULL;
    lv_memzero(dsc, sizeof(lv_sdl_mousewheel_t));

    lv_indev_t * indev = lv_indev_create();
    if(indev == NULL) {
        lv_free(dsc);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, sdl_mousewheel_read);
    lv_indev_set_user_data(indev, dsc);

    return indev;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void sdl_mousewheel_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    lv_sdl_mousewheel_t * dsc = lv_indev_get_user_data(indev);

    data->state = dsc->state;
    data->enc_diff = dsc->diff;
    dsc->diff = 0;
}

void _lv_sdl_mousewheel_handler(SDL_Event * event)
{
    uint32_t win_id = UINT32_MAX;
    switch(event->type) {
        case SDL_MOUSEWHEEL:
            win_id = event->wheel.windowID;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            win_id = event->button.windowID;
            break;
        default:
            return;
    }

    lv_disp_t * disp = _lv_sdl_get_disp_from_win_id(win_id);

    /*Find a suitable indev*/
    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_disp(indev) == disp && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            break;
        }
        indev = lv_indev_get_next(indev);
    }

    if(indev == NULL) return;
    lv_sdl_mousewheel_t * dsc = lv_indev_get_user_data(indev);

    switch(event->type) {
        case SDL_MOUSEWHEEL:
            // Scroll down (y = -1) means positive encoder turn,
            // so invert it
#ifdef __EMSCRIPTEN__
            /*Escripten scales it wrong*/
            if(event->wheel.y < 0) dsc->diff++;
            if(event->wheel.y > 0) dsc->diff--;
#else
            dsc->diff = -event->wheel.y;
#endif
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                dsc->state = LV_INDEV_STATE_PRESSED;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                dsc->state = LV_INDEV_STATE_RELEASED;
            }
            break;
        default:
            break;
    }
}

#endif /*LV_USE_SDL*/

