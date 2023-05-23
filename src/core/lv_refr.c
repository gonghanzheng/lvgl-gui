/**
 * @file lv_refr.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stddef.h>
#include "lv_refr.h"
#include "lv_disp.h"
#include "lv_disp_private.h"
#include "../hal/lv_hal_tick.h"
#include "../misc/lv_timer.h"
#include "../misc/lv_math.h"
#include "../misc/lv_gc.h"
#include "../misc/lv_profiler.h"
#include "../draw/lv_draw.h"
#include "../font/lv_font_fmt_txt.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_refr_join_area(void);
static void refr_invalid_areas(void);
static void refr_area(const lv_area_t * area_p);
static void refr_area_part(lv_layer_t * layer);
static lv_obj_t * lv_refr_get_top_obj(const lv_area_t * area_p, lv_obj_t * obj);
static void refr_obj_and_children(lv_layer_t * layer, lv_obj_t * top_obj);
static void refr_obj(lv_layer_t * layer, lv_obj_t * obj);
static uint32_t get_max_row(lv_disp_t * disp, lv_coord_t area_w, lv_coord_t area_h);
static void draw_buf_flush(lv_disp_t * disp);
static void call_flush_cb(lv_disp_t * disp, const lv_area_t * area, lv_color_t * color_p);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_disp_t * disp_refr; /*Display being refreshed*/

/**********************
 *      MACROS
 **********************/
#if LV_LOG_TRACE_DISP_REFR
    #define REFR_TRACE(...) LV_LOG_TRACE(__VA_ARGS__)
#else
    #define REFR_TRACE(...)
#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the screen refresh subsystem
 */
void _lv_refr_init(void)
{
}

void lv_refr_now(lv_disp_t * disp)
{
    lv_anim_refr_now();

    if(disp) {
        if(disp->refr_timer) _lv_disp_refr_timer(disp->refr_timer);
    }
    else {
        lv_disp_t * d;
        d = lv_disp_get_next(NULL);
        while(d) {
            if(d->refr_timer) _lv_disp_refr_timer(d->refr_timer);
            d = lv_disp_get_next(d);
        }
    }
}

void lv_obj_redraw(lv_layer_t * layer, lv_obj_t * obj)
{
    lv_area_t clip_area_ori = layer->clip_area;
    lv_area_t clip_coords_for_obj;

    /*Truncate the clip area to `obj size + ext size` area*/
    lv_area_t obj_coords_ext;
    lv_obj_get_coords(obj, &obj_coords_ext);
    lv_coord_t ext_draw_size = _lv_obj_get_ext_draw_size(obj);
    lv_area_increase(&obj_coords_ext, ext_draw_size, ext_draw_size);
    bool com_clip_res = _lv_area_intersect(&clip_coords_for_obj, &clip_area_ori, &obj_coords_ext);
    /*If the object is visible on the current clip area*/
    bool should_draw = com_clip_res;
    if(should_draw) {
        layer->clip_area = clip_coords_for_obj;

        lv_obj_send_event(obj, LV_EVENT_DRAW_MAIN_BEGIN, layer);
        lv_obj_send_event(obj, LV_EVENT_DRAW_MAIN, layer);
        lv_obj_send_event(obj, LV_EVENT_DRAW_MAIN_END, layer);
#if LV_USE_REFR_DEBUG
        lv_color_t debug_color = lv_color_make(lv_rand(0, 0xFF), lv_rand(0, 0xFF), lv_rand(0, 0xFF));
        lv_draw_rect_dsc_t draw_dsc;
        lv_draw_rect_dsc_init(&draw_dsc);
        draw_dsc.bg_color = debug_color;
        draw_dsc.bg_opa = LV_OPA_20;
        draw_dsc.border_width = 1;
        draw_dsc.border_opa = LV_OPA_30;
        draw_dsc.border_color = debug_color;
        lv_draw_rect(layer, &draw_dsc, &obj_coords_ext);
#endif
    }

    /*With overflow visible keep the previous clip area to let the children visible out of this object too
     *With not overflow visible limit the clip are to the object's coordinates to clip the children*/
    lv_area_t clip_coords_for_children;
    bool refr_children = true;
    if(!_lv_area_intersect(&clip_coords_for_children, &clip_area_ori, &obj->coords)) {
        refr_children = false;
    }

    if(refr_children) {
        layer->clip_area = clip_coords_for_children;
        uint32_t i;
        uint32_t child_cnt = lv_obj_get_child_cnt(obj);
        if(child_cnt) {
            lv_layer_t * layer_children = layer;
            bool clip_corner = lv_obj_get_style_clip_corner(obj, LV_PART_MAIN);

            lv_coord_t radius = 0;
            if(clip_corner) {
                radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
                if(radius == 0) clip_corner = false;
            }
            if(clip_corner) {
                layer_children = lv_draw_layer_create(layer, LV_COLOR_FORMAT_ARGB8888, &obj->coords);
            }
            for(i = 0; i < child_cnt; i++) {
                lv_obj_t * child = obj->spec_attr->children[i];
                refr_obj(layer_children, child);
            }
            if(clip_corner) {
                lv_draw_mask_rect_dsc_t mask_draw_dsc;
                lv_draw_mask_rect_dsc_init(&mask_draw_dsc);
                mask_draw_dsc.area = layer_children->buf_area;
                mask_draw_dsc.radius = radius;
                lv_draw_mask_rect(layer_children, &mask_draw_dsc);

                lv_draw_img_dsc_t img_draw_dsc;
                lv_draw_img_dsc_init(&img_draw_dsc);
                img_draw_dsc.src = layer_children;
                lv_draw_layer(layer, &img_draw_dsc, &layer_children->buf_area);
            }
        }
    }

    /*If the object was visible on the clip area call the post draw events too*/
    if(should_draw) {
        layer->clip_area = clip_coords_for_obj;

        /*If all the children are redrawn make 'post draw' draw*/
        lv_obj_send_event(obj, LV_EVENT_DRAW_POST_BEGIN, layer);
        lv_obj_send_event(obj, LV_EVENT_DRAW_POST, layer);
        lv_obj_send_event(obj, LV_EVENT_DRAW_POST_END, layer);
    }

    layer->clip_area = clip_area_ori;
}


/**
 * Invalidate an area on display to redraw it
 * @param disp pointer to display where the area should be invalidated (NULL can be used if there is
 * @param area_p pointer to area which should be invalidated (NULL: delete the invalidated areas)
 * only one display)
 */
void _lv_inv_area(lv_disp_t * disp, const lv_area_t * area_p)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) return;
    if(!lv_disp_is_invalidation_enabled(disp)) return;

    if(disp->rendering_in_progress) {
        LV_LOG_ERROR("detected modifying dirty areas in render");
        return;
    }

    /*Clear the invalidate buffer if the parameter is NULL*/
    if(area_p == NULL) {
        disp->inv_p = 0;
        return;
    }

    lv_area_t scr_area;
    scr_area.x1 = 0;
    scr_area.y1 = 0;
    scr_area.x2 = lv_disp_get_hor_res(disp) - 1;
    scr_area.y2 = lv_disp_get_ver_res(disp) - 1;

    lv_area_t com_area;
    bool suc;

    suc = _lv_area_intersect(&com_area, area_p, &scr_area);
    if(suc == false)  return; /*Out of the screen*/

    /*If there were at least 1 invalid area in full refresh mode, redraw the whole screen*/
    if(disp->render_mode == LV_DISP_RENDER_MODE_FULL) {
        disp->inv_areas[0] = scr_area;
        disp->inv_p = 1;
        if(disp->refr_timer) lv_timer_resume(disp->refr_timer);
        return;
    }

    lv_res_t res = lv_disp_send_event(disp, LV_EVENT_INVALIDATE_AREA, &com_area);
    if(res != LV_RES_OK) return;

    /*Save only if this area is not in one of the saved areas*/
    uint16_t i;
    for(i = 0; i < disp->inv_p; i++) {
        if(_lv_area_is_in(&com_area, &disp->inv_areas[i], 0) != false) return;
    }

    /*Save the area*/
    lv_area_t * tmp_area_p = &com_area;
    if(disp->inv_p >= LV_INV_BUF_SIZE) { /*If no place for the area add the screen*/
        disp->inv_p = 0;
        tmp_area_p = &scr_area;
    }
    lv_area_copy(&disp->inv_areas[disp->inv_p], tmp_area_p);
    disp->inv_p++;

    if(disp->refr_timer) lv_timer_resume(disp->refr_timer);
}

/**
 * Get the display which is being refreshed
 * @return the display being refreshed
 */
lv_disp_t * _lv_refr_get_disp_refreshing(void)
{
    return disp_refr;
}

/**
 * Called periodically to handle the refreshing
 * @param tmr pointer to the timer itself
 */
void _lv_disp_refr_timer(lv_timer_t * tmr)
{
    LV_PROFILER_BEGIN;
    REFR_TRACE("begin");

    uint32_t start = lv_tick_get();


    if(tmr) {
        disp_refr = tmr->user_data;
#if LV_USE_PERF_MONITOR == 0 && LV_USE_MEM_MONITOR == 0
        /**
         * Ensure the timer does not run again automatically.
         * This is done before refreshing in case refreshing invalidates something else.
         */
        lv_timer_pause(tmr);
#endif
    }
    else {
        disp_refr = lv_disp_get_default();
    }

    if(disp_refr == NULL) {
        LV_LOG_WARN("No display registered");
        return;
    }

    if(disp_refr->draw_buf_size == 0) {
        LV_LOG_WARN("draw_buf_size == 0");
        return;
    }

    if(disp_refr->draw_buf_1 == NULL) {
        LV_LOG_WARN("No draw buffer");
        return;
    }

    lv_disp_send_event(disp_refr, LV_EVENT_REFR_START, NULL);

    disp_refr->last_render_start_time = start;

    /*Refresh the screen's layout if required*/
    lv_obj_update_layout(disp_refr->act_scr);
    if(disp_refr->prev_scr) lv_obj_update_layout(disp_refr->prev_scr);

    lv_obj_update_layout(disp_refr->bottom_layer);
    lv_obj_update_layout(disp_refr->top_layer);
    lv_obj_update_layout(disp_refr->sys_layer);

    /*Do nothing if there is no active screen*/
    if(disp_refr->act_scr == NULL) {
        disp_refr->inv_p = 0;
        LV_LOG_WARN("there is no active screen");
        goto refr_finish;
    }

    lv_refr_join_area();

    refr_invalid_areas();

    if(disp_refr->inv_p == 0) goto refr_finish;

    /*If refresh happened ...*/
    /*Call monitor cb if present*/
    lv_disp_send_event(disp_refr, LV_EVENT_RENDER_READY, NULL);

    if(!lv_disp_is_double_buffered(disp_refr) || disp_refr->render_mode != LV_DISP_RENDER_MODE_DIRECT) goto refr_clean_up;

    /*With double buffered direct mode synchronize the rendered areas to the other buffer*/
    /*We need to wait for ready here to not mess up the active screen*/
    while(disp_refr->flushing) {
        if(disp_refr->wait_cb) disp_refr->wait_cb(disp_refr);
    }
    /*The buffers are already swapped.
     *So the active buffer is the off screen buffer where LVGL will render*/
    void * buf_off_screen = disp_refr->draw_buf_act;
    void * buf_on_screen = disp_refr->draw_buf_act == disp_refr->draw_buf_1
                           ? disp_refr->draw_buf_2
                           : disp_refr->draw_buf_1;

    lv_coord_t stride = lv_disp_get_hor_res(disp_refr);
    uint32_t i;
    for(i = 0; i < disp_refr->inv_p; i++) {
        if(disp_refr->inv_area_joined[i]) continue;
        disp_refr->layer_head->buffer_copy(
            disp_refr->layer_head,
            buf_off_screen, stride, &disp_refr->inv_areas[i],
            buf_on_screen, stride, &disp_refr->inv_areas[i]
        );
    }

refr_clean_up:
    lv_memzero(disp_refr->inv_areas, sizeof(disp_refr->inv_areas));
    lv_memzero(disp_refr->inv_area_joined, sizeof(disp_refr->inv_area_joined));
    disp_refr->inv_p = 0;

#if LV_USE_DRAW_MASKS
    _lv_draw_sw_mask_cleanup();
#endif

refr_finish:
    lv_disp_send_event(disp_refr, LV_EVENT_REFR_FINISH, NULL);

    REFR_TRACE("finished");
    LV_PROFILER_END;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Join the areas which has got common parts
 */
static void lv_refr_join_area(void)
{
    uint32_t join_from;
    uint32_t join_in;
    lv_area_t joined_area;
    for(join_in = 0; join_in < disp_refr->inv_p; join_in++) {
        if(disp_refr->inv_area_joined[join_in] != 0) continue;

        /*Check all areas to join them in 'join_in'*/
        for(join_from = 0; join_from < disp_refr->inv_p; join_from++) {
            /*Handle only unjoined areas and ignore itself*/
            if(disp_refr->inv_area_joined[join_from] != 0 || join_in == join_from) {
                continue;
            }

            /*Check if the areas are on each other*/
            if(_lv_area_is_on(&disp_refr->inv_areas[join_in], &disp_refr->inv_areas[join_from]) == false) {
                continue;
            }

            _lv_area_join(&joined_area, &disp_refr->inv_areas[join_in], &disp_refr->inv_areas[join_from]);

            /*Join two area only if the joined area size is smaller*/
            if(lv_area_get_size(&joined_area) < (lv_area_get_size(&disp_refr->inv_areas[join_in]) +
                                                 lv_area_get_size(&disp_refr->inv_areas[join_from]))) {
                lv_area_copy(&disp_refr->inv_areas[join_in], &joined_area);

                /*Mark 'join_form' is joined into 'join_in'*/
                disp_refr->inv_area_joined[join_from] = 1;
            }
        }
    }
}

/**
 * Refresh the joined areas
 */
static void refr_invalid_areas(void)
{
    if(disp_refr->inv_p == 0) return;
    LV_PROFILER_BEGIN;

    /*Find the last area which will be drawn*/
    int32_t i;
    int32_t last_i = 0;
    for(i = disp_refr->inv_p - 1; i >= 0; i--) {
        if(disp_refr->inv_area_joined[i] == 0) {
            last_i = i;
            break;
        }
    }

    /*Notify the display driven rendering has started*/
    lv_disp_send_event(disp_refr, LV_EVENT_RENDER_START, NULL);

    disp_refr->last_area = 0;
    disp_refr->last_part = 0;
    disp_refr->rendering_in_progress = true;

    for(i = 0; i < disp_refr->inv_p; i++) {
        /*Refresh the unjoined areas*/
        if(disp_refr->inv_area_joined[i] == 0) {

            if(i == last_i) disp_refr->last_area = 1;
            disp_refr->last_part = 0;
            refr_area(&disp_refr->inv_areas[i]);
        }
    }

    disp_refr->rendering_in_progress = false;
    LV_PROFILER_END;
}

/**
 * Refresh an area if there is Virtual Display Buffer
 * @param area_p  pointer to an area to refresh
 */
static void refr_area(const lv_area_t * area_p)
{
    lv_layer_t * layer = disp_refr->layer_head;
    layer->buf = disp_refr->draw_buf_act;

    /*With full refresh just redraw directly into the buffer*/
    /*In direct mode draw directly on the absolute coordinates of the buffer*/
    if(disp_refr->render_mode != LV_DISP_RENDER_MODE_PARTIAL) {
        lv_area_t disp_area;
        lv_area_set(&disp_area, 0, 0, lv_disp_get_hor_res(disp_refr) - 1,
                    lv_disp_get_ver_res(disp_refr) - 1);
        layer->buf_area = disp_area;

        if(disp_refr->render_mode == LV_DISP_RENDER_MODE_FULL) {
            disp_refr->last_part = 1;
            layer->clip_area = disp_area;
            refr_area_part(layer);
        }
        else if(disp_refr->render_mode == LV_DISP_RENDER_MODE_DIRECT) {
            disp_refr->last_part = disp_refr->last_area;
            layer->clip_area = *area_p;
            refr_area_part(layer);
        }
        return;
    }

    /*Normal refresh: draw the area in parts*/
    /*Calculate the max row num*/
    lv_coord_t w = lv_area_get_width(area_p);
    lv_coord_t h = lv_area_get_height(area_p);
    lv_coord_t y2 = area_p->y2 >= lv_disp_get_ver_res(disp_refr) ?
                    lv_disp_get_ver_res(disp_refr) - 1 : area_p->y2;

    int32_t max_row = get_max_row(disp_refr, w, h);

    lv_coord_t row;
    lv_coord_t row_last = 0;
    lv_area_t sub_area;
    for(row = area_p->y1; row + max_row - 1 <= y2; row += max_row) {
        /*Calc. the next y coordinates of draw_buf*/
        sub_area.x1 = area_p->x1;
        sub_area.x2 = area_p->x2;
        sub_area.y1 = row;
        sub_area.y2 = row + max_row - 1;
        layer->buf_area = sub_area;
        layer->clip_area = sub_area;
        layer->buf = disp_refr->draw_buf_act;
        if(sub_area.y2 > y2) sub_area.y2 = y2;
        row_last = sub_area.y2;
        if(y2 == row_last) disp_refr->last_part = 1;
        refr_area_part(layer);
    }

    /*If the last y coordinates are not handled yet ...*/
    if(y2 != row_last) {
        /*Calc. the next y coordinates of draw_buf*/
        sub_area.x1 = area_p->x1;
        sub_area.x2 = area_p->x2;
        sub_area.y1 = row;
        sub_area.y2 = y2;
        layer->buf_area = sub_area;
        layer->clip_area = sub_area;
        layer->buf = disp_refr->draw_buf_act;
        disp_refr->last_part = 1;
        refr_area_part(layer);
    }
}

static void refr_area_part(lv_layer_t * layer)
{
    disp_refr->refreshed_area = layer->clip_area;

    /* In single buffered mode wait here until the buffer is freed.
     * Else we would draw into the buffer while it's still being transferred to the display*/
    if(!lv_disp_is_double_buffered(disp_refr)) {
        while(disp_refr->flushing) {
            if(disp_refr->wait_cb) disp_refr->wait_cb(disp_refr);
        }
    }
    /*If the screen is transparent initialize it when the flushing is ready*/
    if(lv_color_format_has_alpha(disp_refr->color_format)) {
        if(layer->buffer_clear) layer->buffer_clear(layer);
    }

    lv_obj_t * top_act_scr = NULL;
    lv_obj_t * top_prev_scr = NULL;

    /*Get the most top object which is not covered by others*/
    top_act_scr = lv_refr_get_top_obj(&layer->buf_area, lv_disp_get_scr_act(disp_refr));
    if(disp_refr->prev_scr) {
        top_prev_scr = lv_refr_get_top_obj(&layer->buf_area, disp_refr->prev_scr);
    }

    /*Draw a bottom layer background if there is no top object*/
    if(top_act_scr == NULL && top_prev_scr == NULL) {
        refr_obj_and_children(layer, lv_disp_get_layer_bottom(disp_refr));
    }

    if(disp_refr->draw_prev_over_act) {
        if(top_act_scr == NULL) top_act_scr = disp_refr->act_scr;
        refr_obj_and_children(layer, top_act_scr);

        /*Refresh the previous screen if any*/
        if(disp_refr->prev_scr) {
            if(top_prev_scr == NULL) top_prev_scr = disp_refr->prev_scr;
            refr_obj_and_children(layer, top_prev_scr);
        }
    }
    else {
        /*Refresh the previous screen if any*/
        if(disp_refr->prev_scr) {
            if(top_prev_scr == NULL) top_prev_scr = disp_refr->prev_scr;
            refr_obj_and_children(layer, top_prev_scr);
        }

        if(top_act_scr == NULL) top_act_scr = disp_refr->act_scr;
        refr_obj_and_children(layer, top_act_scr);
    }

    /*Also refresh top and sys layer unconditionally*/
    refr_obj_and_children(layer, lv_disp_get_layer_top(disp_refr));
    refr_obj_and_children(layer, lv_disp_get_layer_sys(disp_refr));

    draw_buf_flush(disp_refr);
}

/**
 * Search the most top object which fully covers an area
 * @param area_p pointer to an area
 * @param obj the first object to start the searching (typically a screen)
 * @return
 */
static lv_obj_t * lv_refr_get_top_obj(const lv_area_t * area_p, lv_obj_t * obj)
{
    lv_obj_t * found_p = NULL;

    if(_lv_area_is_in(area_p, &obj->coords, 0) == false) return NULL;
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if(_lv_obj_get_layer_type(obj) != LV_LAYER_TYPE_NONE) return NULL;

    /*If this object is fully cover the draw area then check the children too*/
    lv_cover_check_info_t info;
    info.res = LV_COVER_RES_COVER;
    info.area = area_p;
    lv_obj_send_event(obj, LV_EVENT_COVER_CHECK, &info);
    if(info.res == LV_COVER_RES_MASKED) return NULL;

    int32_t i;
    int32_t child_cnt = lv_obj_get_child_cnt(obj);
    for(i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t * child = obj->spec_attr->children[i];
        found_p = lv_refr_get_top_obj(area_p, child);

        /*If a children is ok then break*/
        if(found_p != NULL) {
            break;
        }
    }

    /*If no better children use this object*/
    if(found_p == NULL && info.res == LV_COVER_RES_COVER) {
        found_p = obj;
    }

    return found_p;
}

/**
 * Make the refreshing from an object. Draw all its children and the youngers too.
 * @param top_p pointer to an objects. Start the drawing from it.
 * @param mask_p pointer to an area, the objects will be drawn only here
 */
static void refr_obj_and_children(lv_layer_t * layer, lv_obj_t * top_obj)
{
    /*Normally always will be a top_obj (at least the screen)
     *but in special cases (e.g. if the screen has alpha) it won't.
     *In this case use the screen directly*/
    if(top_obj == NULL) top_obj = lv_disp_get_scr_act(disp_refr);
    if(top_obj == NULL) return;  /*Shouldn't happen*/

    /*Refresh the top object and its children*/
    refr_obj(layer, top_obj);

    /*Draw the 'younger' sibling objects because they can be on top_obj*/
    lv_obj_t * parent;
    lv_obj_t * border_p = top_obj;

    parent = lv_obj_get_parent(top_obj);

    /*Do until not reach the screen*/
    while(parent != NULL) {
        bool go = false;
        uint32_t i;
        uint32_t child_cnt = lv_obj_get_child_cnt(parent);
        for(i = 0; i < child_cnt; i++) {
            lv_obj_t * child = parent->spec_attr->children[i];
            if(!go) {
                if(child == border_p) go = true;
            }
            else {
                /*Refresh the objects*/
                refr_obj(layer, child);
            }
        }

        /*Call the post draw draw function of the parents of the to object*/
        lv_obj_send_event(parent, LV_EVENT_DRAW_POST_BEGIN, (void *)layer);
        lv_obj_send_event(parent, LV_EVENT_DRAW_POST, (void *)layer);
        lv_obj_send_event(parent, LV_EVENT_DRAW_POST_END, (void *)layer);

        /*The new border will be the last parents,
         *so the 'younger' brothers of parent will be refreshed*/
        border_p = parent;
        /*Go a level deeper*/
        parent = lv_obj_get_parent(parent);
    }
}


static lv_res_t layer_get_area(lv_layer_t * layer, lv_obj_t * obj, lv_layer_type_t layer_type,
                               lv_area_t * layer_area_out)
{
    lv_coord_t ext_draw_size = _lv_obj_get_ext_draw_size(obj);
    lv_area_t obj_coords_ext;
    lv_obj_get_coords(obj, &obj_coords_ext);
    lv_area_increase(&obj_coords_ext, ext_draw_size, ext_draw_size);

    if(layer_type == LV_LAYER_TYPE_TRANSFORM) {
        /*Get the transformed area and clip it to the current clip area.
         *This area needs to be updated on the screen.*/
        lv_area_t clip_coords_for_obj;
        lv_area_t tranf_coords = obj_coords_ext;
        lv_obj_get_transformed_area(obj, &tranf_coords, false, false);
        if(!_lv_area_intersect(&clip_coords_for_obj, &layer->clip_area, &tranf_coords)) {
            return LV_RES_INV;
        }

        /*Transform back (inverse) the transformed area.
         *It will tell which area of the non-transformed widget needs to be redrawn
         *in order to cover transformed area after transformation.*/
        lv_area_t inverse_clip_coords_for_obj = clip_coords_for_obj;
        lv_obj_get_transformed_area(obj, &inverse_clip_coords_for_obj, false, true);
        if(!_lv_area_intersect(&inverse_clip_coords_for_obj, &inverse_clip_coords_for_obj, &obj_coords_ext)) {
            return LV_RES_INV;
        }

        *layer_area_out = inverse_clip_coords_for_obj;
        lv_area_increase(layer_area_out, 1, 1); /*To avoid rounding error*/
    }
    else if(layer_type == LV_LAYER_TYPE_SIMPLE) {
        lv_area_t clip_coords_for_obj;
        if(!_lv_area_intersect(&clip_coords_for_obj, &layer->clip_area, &obj_coords_ext)) {
            return LV_RES_INV;
        }
        *layer_area_out = clip_coords_for_obj;
    }
    else {
        LV_LOG_WARN("Unhandled layer type");
        return LV_RES_INV;
    }

    return LV_RES_OK;
}


static bool alpha_test_area_on_obj(lv_obj_t * obj, const lv_area_t * area)
{

    /*Test for alpha by assuming there is no alpha. If it fails, fall back to rendering with alpha*/
    if(!_lv_area_is_on(area, &obj->coords)) return false;

    lv_cover_check_info_t info;
    info.res = LV_COVER_RES_COVER;
    info.area = area;
    lv_obj_send_event(obj, LV_EVENT_COVER_CHECK, &info);
    if(info.res == LV_COVER_RES_COVER) return false;
    else return true;
}


void refr_obj(lv_layer_t * layer, lv_obj_t * obj)
{
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;

    lv_layer_type_t layer_type = _lv_obj_get_layer_type(obj);
    if(layer_type == LV_LAYER_TYPE_NONE) {
        lv_obj_redraw(layer, obj);
    }
    else {
        lv_opa_t opa = lv_obj_get_style_opa(obj, 0);
        if(opa < LV_OPA_MIN) return;

        lv_area_t layer_area_full;
        lv_res_t res = layer_get_area(layer, obj, layer_type, &layer_area_full);
        if(res != LV_RES_OK) return;

        /*Simple layers can be subdivied into smaller layers*/
        uint32_t max_rgb_row_height = lv_area_get_height(&layer_area_full);
        uint32_t max_argb_row_height = lv_area_get_height(&layer_area_full);
        if(layer_type == LV_LAYER_TYPE_SIMPLE) {
            lv_coord_t w = lv_area_get_width(&layer_area_full);
            uint8_t px_size = lv_color_format_get_size(disp_refr->color_format);
            max_rgb_row_height = LV_DRAW_SW_LAYER_SIMPLE_BUF_SIZE / w / px_size;
            max_argb_row_height = LV_DRAW_SW_LAYER_SIMPLE_BUF_SIZE / w / sizeof(lv_color32_t);
        }

        lv_area_t layer_area_act;
        layer_area_act.x1 = layer_area_full.x1;
        layer_area_act.x2 = layer_area_full.x2;
        layer_area_act.y1 = layer_area_full.y1;
        layer_area_act.y2 = layer_area_full.y1;

        while(layer_area_act.y2 < layer_area_full.y2) {
            /* Test with an RGB layer size (which is larger than the ARGB layer size)
             * If it really doesn't need alpha use it. Else switch to the ARGB size*/
            layer_area_act.y2 = layer_area_act.y1 + max_rgb_row_height - 1;
            if(layer_area_act.y2 > layer_area_full.y2) layer_area_act.y2 = layer_area_full.y2;
            bool area_need_alpha = alpha_test_area_on_obj(obj, &layer_area_act);
            if(area_need_alpha) {
                layer_area_act.y2 = layer_area_act.y1 + max_argb_row_height - 1;
                if(layer_area_act.y2 > layer_area_full.y2) layer_area_act.y2 = layer_area_full.y2;
            }

            lv_layer_t * new_layer = lv_draw_layer_create(layer,
                                                          area_need_alpha ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_NATIVE, &layer_area_act);
            lv_obj_redraw(new_layer, obj);

            lv_draw_img_dsc_t layer_draw_dsc;
            lv_draw_img_dsc_init(&layer_draw_dsc);
            layer_draw_dsc.pivot.x = obj->coords.x1 + lv_obj_get_style_transform_pivot_x(obj, 0) - new_layer->buf_area.x1;
            layer_draw_dsc.pivot.y = obj->coords.y1 + lv_obj_get_style_transform_pivot_y(obj, 0) - new_layer->buf_area.y1;

            layer_draw_dsc.opa = opa;
            layer_draw_dsc.angle = lv_obj_get_style_transform_angle(obj, 0);
            while(layer_draw_dsc.angle > 3600) layer_draw_dsc.angle -= 3600;
            while(layer_draw_dsc.angle < 0) layer_draw_dsc.angle += 3600;
            layer_draw_dsc.zoom = lv_obj_get_style_transform_zoom(obj, 0);
            layer_draw_dsc.blend_mode = lv_obj_get_style_blend_mode(obj, 0);
            layer_draw_dsc.antialias = disp_refr->antialiasing;
            layer_draw_dsc.src = new_layer;

            lv_draw_layer(layer, &layer_draw_dsc, &new_layer->buf_area);

            layer_area_act.y1 = layer_area_act.y2 + 1;
        }
    }
}


static uint32_t get_max_row(lv_disp_t * disp, lv_coord_t area_w, lv_coord_t area_h)
{
    bool has_alpha = lv_color_format_has_alpha(disp->color_format);
    uint32_t px_size_disp =  lv_color_format_get_size(disp->color_format);
    uint8_t px_size_render = has_alpha ? sizeof(lv_color32_t) : px_size_disp;
    int32_t max_row = (uint32_t)disp->draw_buf_size / LV_MAX(px_size_render, px_size_disp) / area_w;

    if(max_row > area_h) max_row = area_h;

    /*Round down the lines of draw_buf if rounding is added*/
    lv_area_t tmp;
    tmp.x1 = 0;
    tmp.x2 = 0;
    tmp.y1 = 0;

    lv_coord_t h_tmp = max_row;
    do {
        tmp.y2 = h_tmp - 1;
        lv_disp_send_event(disp_refr, LV_EVENT_INVALIDATE_AREA, &tmp);

        /*If this height fits into `max_row` then fine*/
        if(lv_area_get_height(&tmp) <= max_row) break;

        /*Decrement the height of the area until it fits into `max_row` after rounding*/
        h_tmp--;
    } while(h_tmp > 0);

    if(h_tmp <= 0) {
        LV_LOG_WARN("Can't set draw_buf height using the round function. (Wrong round_cb or too "
                    "small draw_buf)");
        return 0;
    }
    else {
        max_row = tmp.y2 + 1;
    }

    return max_row;
}

static void draw_buf_rotate_180(lv_disp_t * disp, lv_area_t * area, lv_color_t * color_p)
{
    lv_coord_t area_w = lv_area_get_width(area);
    lv_coord_t area_h = lv_area_get_height(area);
    uint32_t total = area_w * area_h;
    /*Swap the beginning and end values*/
    lv_color_t tmp;
    uint32_t i = total - 1, j = 0;
    while(i > j) {
        tmp = color_p[i];
        color_p[i] = color_p[j];
        color_p[j] = tmp;
        i--;
        j++;
    }
    lv_coord_t tmp_coord;
    tmp_coord = area->y2;
    area->y2 = disp->ver_res - area->y1 - 1;
    area->y1 = disp->ver_res - tmp_coord - 1;
    tmp_coord = area->x2;
    area->x2 = disp->hor_res - area->x1 - 1;
    area->x1 = disp->hor_res - tmp_coord - 1;
}

static LV_ATTRIBUTE_FAST_MEM void draw_buf_rotate_90(bool invert_i, lv_coord_t area_w, lv_coord_t area_h,
                                                     lv_color_t * orig_color_p, lv_color_t * rot_buf)
{

    uint32_t invert = (area_w * area_h) - 1;
    uint32_t initial_i = ((area_w - 1) * area_h);
    for(lv_coord_t y = 0; y < area_h; y++) {
        uint32_t i = initial_i + y;
        if(invert_i)
            i = invert - i;
        for(lv_coord_t x = 0; x < area_w; x++) {
            rot_buf[i] = *(orig_color_p++);
            if(invert_i)
                i += area_h;
            else
                i -= area_h;
        }
    }
}

/**
 * Helper function for draw_buf_rotate_90_sqr. Given a list of four numbers, rotate the entire list to the left.
 */
static inline void draw_buf_rotate4(lv_color_t * a, lv_color_t * b, lv_color_t * c, lv_color_t * d)
{
    lv_color_t tmp;
    tmp = *a;
    *a = *b;
    *b = *c;
    *c = *d;
    *d = tmp;
}

/**
 * Rotate a square image 90/270 degrees in place.
 * @note inspired by https://stackoverflow.com/a/43694906
 */
static void draw_buf_rotate_90_sqr(bool is_270, lv_coord_t w, lv_color_t * color_p)
{
    for(lv_coord_t i = 0; i < w / 2; i++) {
        for(lv_coord_t j = 0; j < (w + 1) / 2; j++) {
            lv_coord_t inv_i = (w - 1) - i;
            lv_coord_t inv_j = (w - 1) - j;
            if(is_270) {
                draw_buf_rotate4(
                    &color_p[i * w + j],
                    &color_p[inv_j * w + i],
                    &color_p[inv_i * w + inv_j],
                    &color_p[j * w + inv_i]
                );
            }
            else {
                draw_buf_rotate4(
                    &color_p[i * w + j],
                    &color_p[j * w + inv_i],
                    &color_p[inv_i * w + inv_j],
                    &color_p[inv_j * w + i]
                );
            }

        }
    }
}

/**
 * Rotate the draw_buf to the display's native orientation.
 */
static void draw_buf_rotate(lv_area_t * area, lv_color_t * color_p)
{
    if(disp_refr->render_mode == LV_DISP_RENDER_MODE_FULL && disp_refr->sw_rotate) {
        LV_LOG_ERROR("cannot rotate a full refreshed display!");
        return;
    }
    if(disp_refr->rotation == LV_DISP_ROTATION_180) {
        draw_buf_rotate_180(disp_refr, area, color_p);
        call_flush_cb(disp_refr, area, color_p);
    }
    else if(disp_refr->rotation == LV_DISP_ROTATION_90 || disp_refr->rotation == LV_DISP_ROTATION_270) {
        /*Allocate a temporary buffer to store rotated image*/
        lv_color_t * rot_buf = NULL;
        lv_coord_t area_w = lv_area_get_width(area);
        lv_coord_t area_h = lv_area_get_height(area);
        /*Determine the maximum number of rows that can be rotated at a time*/
        lv_coord_t max_row = LV_MIN((lv_coord_t)((LV_DISP_ROT_MAX_BUF / sizeof(lv_color_t)) / area_w), area_h);
        lv_coord_t init_y_off;
        init_y_off = area->y1;
        if(disp_refr->rotation == LV_DISP_ROTATION_90) {
            area->y2 = disp_refr->ver_res - area->x1 - 1;
            area->y1 = area->y2 - area_w + 1;
        }
        else {
            area->y1 = area->x1;
            area->y2 = area->y1 + area_w - 1;
        }

        /*Rotate the screen in chunks, flushing after each one*/
        lv_coord_t row = 0;
        while(row < area_h) {
            lv_coord_t height = LV_MIN(max_row, area_h - row);
            disp_refr->flushing = 1;
            if((row == 0) && (area_h >= area_w)) {
                /*Rotate the initial area as a square*/
                height = area_w;
                draw_buf_rotate_90_sqr(disp_refr->rotation == LV_DISP_ROTATION_270, area_w, color_p);
                if(disp_refr->rotation == LV_DISP_ROTATION_90) {
                    area->x1 = init_y_off;
                    area->x2 = init_y_off + area_w - 1;
                }
                else {
                    area->x2 = disp_refr->hor_res - 1 - init_y_off;
                    area->x1 = area->x2 - area_w + 1;
                }
            }
            else {
                /*Rotate other areas using a maximum buffer size*/
                if(rot_buf == NULL) rot_buf = lv_malloc(LV_DISP_ROT_MAX_BUF);
                draw_buf_rotate_90(disp_refr->rotation == LV_DISP_ROTATION_270, area_w, height, color_p, rot_buf);

                if(disp_refr->rotation == LV_DISP_ROTATION_90) {
                    area->x1 = init_y_off + row;
                    area->x2 = init_y_off + row + height - 1;
                }
                else {
                    area->x2 = disp_refr->hor_res - 1 - init_y_off - row;
                    area->x1 = area->x2 - height + 1;
                }
            }

            /* The original part (chunk of the current area) were split into more parts here.
             * Set the original last_part flag on the last part of rotation. */
            if(row + height >= area_h && disp_refr->last_area && disp_refr->last_part) {
                disp_refr->flushing_last = 1;
            }
            else {
                disp_refr->flushing_last = 0;
            }

            /*Flush the completed area to the display*/
            call_flush_cb(disp_refr, area, rot_buf == NULL ? color_p : rot_buf);
            /*FIXME: Rotation forces legacy behavior where rendering and flushing are done serially*/
            while(disp_refr->flushing) {
                if(disp_refr->wait_cb) disp_refr->wait_cb(disp_refr);
            }
            color_p += area_w * height;
            row += height;
        }
        /*Free the allocated buffer at the end if necessary*/
        if(rot_buf != NULL) lv_free(rot_buf);
    }
}

/**
 * Flush the content of the draw buffer
 */
static void draw_buf_flush(lv_disp_t * disp)
{
    /*Flush the rendered content to the display*/
    lv_layer_t * layer = disp->layer_head;
    //    if(layer->wait_for_finish) layer->wait_for_finish(layer);
    while(layer->draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    /* In double buffered mode wait until the other buffer is freed
     * and driver is ready to receive the new buffer.
     * If we need to wait here it means that the content of one buffer is being sent to display
     * and other buffer already contains the new rendered image. */
    if(lv_disp_is_double_buffered(disp)) {
        while(disp->flushing) {
            if(disp->wait_cb) disp_refr->wait_cb(disp);
        }
    }

    disp->flushing = 1;

    if(disp->last_area && disp->last_part) disp->flushing_last = 1;
    else disp->flushing_last = 0;

    bool flushing_last = disp->flushing_last;

    if(disp->flush_cb) {
        /*Rotate the buffer to the display's native orientation if necessary*/
        if(disp->rotation != LV_DISP_ROTATION_0 && disp->sw_rotate) {
            draw_buf_rotate(&layer->buf_area, layer->buf);
        }
        else {
            call_flush_cb(disp, &disp->refreshed_area, layer->buf);
        }
    }
    /*If there are 2 buffers swap them. With direct mode swap only on the last area*/
    if(lv_disp_is_double_buffered(disp) && (disp->render_mode != LV_DISP_RENDER_MODE_DIRECT || flushing_last)) {
        if(disp->draw_buf_act == disp->draw_buf_1) {
            disp->draw_buf_act = disp->draw_buf_2;
        }
        else {
            disp->draw_buf_act = disp->draw_buf_1;
        }
    }
}

static void call_flush_cb(lv_disp_t * disp, const lv_area_t * area, lv_color_t * color_p)
{
    LV_PROFILER_BEGIN;
    REFR_TRACE("Calling flush_cb on (%d;%d)(%d;%d) area with %p image pointer",
               (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
               (void *)color_p);

    lv_area_t offset_area = {
        .x1 = area->x1 + disp->offset_x,
        .y1 = area->y1 + disp->offset_y,
        .x2 = area->x2 + disp->offset_x,
        .y2 = area->y2 + disp->offset_y
    };

    if(disp->layer_head->buffer_convert) disp->layer_head->buffer_convert(disp->layer_head);

    disp->flush_cb(disp, &offset_area, color_p);
    LV_PROFILER_END;
}
