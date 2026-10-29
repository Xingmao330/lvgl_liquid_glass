/**
 * @file liquid_glass_widget.c
 *
 * Reusable liquid glass layer for LVGL widgets.
 *
 * Every applied widget owns a hidden canvas that holds its glass content.
 * The content is produced by snapshotting the screen without the widget,
 * copying the region behind it into the canvas, then blurring, refracting
 * and tinting it. The canvas image is shown through the widget's
 * bg_img_src style, so it participates in normal rendering (radius
 * clipping, transforms, scrolling) and children stack on top of it.
 *
 * A manager timer walks the active screen's object tree in paint order and
 * refreshes every glass surface back to front. While a surface is refreshed,
 * it and every glass surface above it are hidden for the snapshot, so a
 * surface only ever samples the backdrop plus the glass layers strictly
 * below it. Without this, stacked surfaces would feed their own content
 * back into each other on every refresh and the overlap would wash out.
 */

#include "liquid_glass_widget.h"
#include "liquid_glass_shader.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define LIQUID_GLASS_DEFAULT_PERIOD_MS 33
#define LIQUID_GLASS_TINT_INSET        2

typedef struct liquid_glass_surface {
    lv_obj_t * obj;              /* the user widget carrying the effect */
    lv_obj_t * hide_target;      /* object hidden while snapshotting (obj
                                    by default, may cover sibling overlays) */
    lv_obj_t * canvas;           /* hidden canvas child feeding bg_img_src */
    lv_draw_buf_t * canvas_buf;  /* canvas backing buffer, owned */
    liquid_glass_params_t params;
    int32_t blur_radius;
    int32_t view_scale_x;        /* widget transform compensation, 256 = off */
    int32_t view_scale_y;
    lv_color_t tint_color;
    lv_opa_t tint_opa;
    int32_t inherited_blur;         /* frost of overlapping lower glass */
    bool has_lower_overlap;
    bool live_backdrop;
    bool dirty;
    bool pass_hidden;
    struct liquid_glass_surface * next;
} liquid_glass_surface_t;

typedef struct {
    liquid_glass_surface_t ** order;   /* output: surfaces in paint order */
    uint32_t count;
    uint32_t capacity;
} liquid_glass_collect_ctx_t;

static liquid_glass_surface_t * surface_list;
static lv_timer_t * manager_timer;
static lv_draw_buf_t * screen_snapshot;   /* shared by all surfaces */
static lv_draw_buf_t * live_snapshot;      /* opt-in live glass backdrop */
static lv_draw_buf_t * pass_snapshot;     /* snapshot of the running pass */
static bool pass_gpu_ready;
static liquid_glass_refresh_policy_t refresh_policy = LIQUID_GLASS_REFRESH_ALWAYS;
static uint32_t refresh_period = LIQUID_GLASS_DEFAULT_PERIOD_MS;
static liquid_glass_render_mode_t render_mode = LIQUID_GLASS_RENDER_CPU;
static liquid_glass_shader_t glass_shader;
static bool shader_ready;
static bool shader_failed;

/* Global parameter overrides (-1: use the per-surface value). */
static int32_t global_blur = -1;
static int32_t global_refr_height = -1;
static int32_t global_refr_amount = -1;   /* x10 */
static int32_t global_dispersion = -1;    /* x10 */
static int32_t global_tint_opa = -1;
static bool effects_enabled = true;

static int32_t surface_effective_blur(const liquid_glass_surface_t * s)
{
    return global_blur >= 0 ? global_blur : s->blur_radius;
}

static lv_opa_t surface_effective_tint_opa(const liquid_glass_surface_t * s)
{
    return global_tint_opa >= 0 ? (lv_opa_t)global_tint_opa : s->tint_opa;
}

/* Use the object's actual clip radius for pass compositing. The parameter is
 * still used by the refraction shader, but the snapshot mask must match the
 * radius LVGL uses when it paints the widget or a one pixel white seam can
 * appear where two rounded surfaces touch. */
static int32_t surface_clip_radius(const liquid_glass_surface_t * surface)
{
    int32_t radius = lv_obj_get_style_radius(surface->obj, 0);
    int32_t max_radius = LV_MIN(lv_obj_get_width(surface->obj),
                                lv_obj_get_height(surface->obj));

    if(radius == LV_RADIUS_CIRCLE) {
        radius = max_radius;
    }
    if(radius <= 0) radius = surface->params.corner_radius;
    return radius;
}

static void mark_all_dirty(void)
{
    liquid_glass_surface_t * iter;

    for(iter = surface_list; iter != NULL; iter = iter->next) {
        iter->dirty = true;
    }
}

static liquid_glass_surface_t * find_surface(const lv_obj_t * obj)
{
    liquid_glass_surface_t * iter;

    for(iter = surface_list; iter != NULL; iter = iter->next) {
        if(iter->obj == obj) return iter;
    }
    return NULL;
}

/* Make the canvas buffer match the widget's current size. Returns false when
 * the widget has no size yet (the canvas is created on the next refresh). */
static bool ensure_canvas_geometry(liquid_glass_surface_t * surface)
{
    lv_obj_t * obj = surface->obj;
    lv_draw_buf_t * buf;
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);

    if(w <= 0 || h <= 0) return false;
    if(surface->canvas_buf != NULL &&
       surface->canvas_buf->header.w == (uint32_t)w &&
       surface->canvas_buf->header.h == (uint32_t)h) return true;

    buf = lv_draw_buf_create((uint32_t)w, (uint32_t)h,
                             LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if(buf == NULL) return false;

    lv_draw_buf_clear(buf, NULL);
    lv_canvas_set_draw_buf(surface->canvas, buf);
    lv_obj_set_style_bg_image_src(obj, lv_canvas_get_image(surface->canvas), 0);

    lv_draw_buf_destroy(surface->canvas_buf);
    surface->canvas_buf = buf;
    return true;
}

/* Snapshot the whole screen into a shared buffer. Refreshes run serialized
 * in the LVGL thread and each surface extracts its region immediately, so a
 * single buffer serves every surface: at 320x483 one ARGB8888 snapshot is
 * ~618 KB and per-surface copies would exhaust the LVGL pool with just a
 * handful of glass widgets. */
static lv_draw_buf_t * take_screen_snapshot_to(lv_obj_t * screen,
                                                lv_draw_buf_t ** buffer)
{
    if(screen == NULL) return NULL;
    if(*buffer != NULL) {
        if(lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_ARGB8888,
                                        *buffer) == LV_RESULT_OK) {
            return *buffer;
        }
        /* The cached buffer no longer fits (e.g. the screen grew). */
        lv_draw_buf_destroy(*buffer);
        *buffer = NULL;
    }

    *buffer = lv_snapshot_create_draw_buf(screen, LV_COLOR_FORMAT_ARGB8888);
    if(*buffer == NULL) return NULL;
    if(lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_ARGB8888,
                                    *buffer) != LV_RESULT_OK) {
        return NULL;
    }
    return *buffer;
}

static lv_draw_buf_t * take_screen_snapshot(lv_obj_t * screen)
{
    return take_screen_snapshot_to(screen, &screen_snapshot);
}

/* Screen coordinates of the widget's top-left corner inside the snapshot
 * buffer (the snapshot starts ext-draw-size pixels above/left of the
 * screen origin). */
static void get_snapshot_offsets(const liquid_glass_surface_t * surface,
                                 lv_obj_t * screen,
                                 const lv_draw_buf_t * snapshot,
                                 int32_t * off_x,
                                 int32_t * off_y)
{
    lv_area_t obj_coords;
    lv_area_t screen_coords;

    lv_obj_get_coords(surface->obj, &obj_coords);
    lv_obj_get_coords(screen, &screen_coords);

    *off_x = obj_coords.x1 - screen_coords.x1 +
             ((int32_t)snapshot->header.w - lv_obj_get_width(screen)) / 2;
    *off_y = obj_coords.y1 - screen_coords.y1 +
             ((int32_t)snapshot->header.h - lv_obj_get_height(screen)) / 2;
}

/* Copy the snapshot region behind the widget into the canvas buffer. */
static void extract_backdrop_region(liquid_glass_surface_t * surface,
                                    const lv_draw_buf_t * snapshot,
                                    int32_t off_x,
                                    int32_t off_y)
{
    const uint8_t * src = snapshot->data;
    uint8_t * dst = surface->canvas_buf->data;
    uint32_t src_stride = snapshot->header.stride;
    uint32_t dst_stride = surface->canvas_buf->header.stride;
    int32_t w = (int32_t)surface->canvas_buf->header.w;
    int32_t h = (int32_t)surface->canvas_buf->header.h;
    int32_t snap_w = (int32_t)snapshot->header.w;
    int32_t snap_h = (int32_t)snapshot->header.h;
    int32_t row;
    int32_t col;

    for(row = 0; row < h; row++) {
        int32_t sy = off_y + row;
        int32_t sx = off_x;

        if(sy >= 0 && sy < snap_h && sx >= 0 && sx + w <= snap_w) {
            memcpy(dst + (size_t)row * dst_stride,
                   src + (size_t)sy * src_stride + (size_t)sx * 4U,
                   (size_t)w * 4U);
            continue;
        }

        /* Clamp rows/columns that fall outside the snapshot. */
        if(sy < 0) sy = 0;
        if(sy >= snap_h) sy = snap_h - 1;
        for(col = 0; col < w; col++) {
            int32_t scol = off_x + col;

            if(scol < 0) scol = 0;
            if(scol >= snap_w) scol = snap_w - 1;
            memcpy(dst + (size_t)row * dst_stride + (size_t)col * 4U,
                   src + (size_t)sy * src_stride + (size_t)scol * 4U,
                   4U);
        }
    }
}

/* Backdrop extraction while the widget is rendered scaled: the widget
 * transform will zoom whatever is in the canvas by k = view_scale/256, so
 * draw the central region of the backdrop magnified by 1/k instead. The
 * background then stays visually pinned while only the glass edges deform. */
static void extract_backdrop_scaled(liquid_glass_surface_t * surface,
                                    const lv_draw_buf_t * snapshot,
                                    int32_t off_x,
                                    int32_t off_y)
{
    int32_t w = (int32_t)surface->canvas_buf->header.w;
    int32_t h = (int32_t)surface->canvas_buf->header.h;
    int32_t snap_w = (int32_t)snapshot->header.w;
    int32_t snap_h = (int32_t)snapshot->header.h;
    int32_t sub_w = w * surface->view_scale_x / LV_SCALE_NONE;
    int32_t sub_h = h * surface->view_scale_y / LV_SCALE_NONE;
    int32_t sub_x;
    int32_t sub_y;
    lv_image_dsc_t sub;
    lv_layer_t layer;
    lv_draw_image_dsc_t dsc;
    lv_area_t coords;

    if(sub_w < 1) sub_w = 1;
    if(sub_h < 1) sub_h = 1;
    /* Match the parity of w/h so the sub rect, its pivot and the canvas
     * center all land on whole pixels; accumulated floor-rounding would
     * otherwise shift the pinned backdrop by a couple of pixels. */
    sub_w += (w - sub_w) & 1;
    sub_h += (h - sub_h) & 1;
    sub_x = off_x + (w - sub_w) / 2;
    sub_y = off_y + (h - sub_h) / 2;

    /* No compensation when the magnified region would leave the snapshot. */
    if(sub_x < 0 || sub_y < 0 ||
       sub_x + sub_w > snap_w || sub_y + sub_h > snap_h) {
        extract_backdrop_region(surface, snapshot, off_x, off_y);
        return;
    }

    /* Plain extract first so integer rounding in the scaled draw (which can
     * leave a 1px sliver uncovered) shows plausible content. */
    extract_backdrop_region(surface, snapshot, off_x, off_y);

    /* Zero-copy view into the shared snapshot buffer. data_size must cover
     * the view or lv_draw_buf_init rejects it and the scaled draw silently
     * fails, leaving the stale (uncompensated) backdrop in the canvas. */
    lv_memzero(&sub, sizeof(sub));
    sub.header.cf = LV_COLOR_FORMAT_ARGB8888;
    sub.header.w = (uint32_t)sub_w;
    sub.header.h = (uint32_t)sub_h;
    sub.header.stride = snapshot->header.stride;
    sub.data = snapshot->data +
               (size_t)sub_y * snapshot->header.stride + (size_t)sub_x * 4U;
    sub.data_size = snapshot->data_size -
                    ((size_t)sub_y * snapshot->header.stride +
                     (size_t)sub_x * 4U);

    lv_canvas_init_layer(surface->canvas, &layer);
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &sub;
    dsc.scale_x = (uint32_t)LV_SCALE_NONE * LV_SCALE_NONE /
                  (uint32_t)surface->view_scale_x;
    dsc.scale_y = (uint32_t)LV_SCALE_NONE * LV_SCALE_NONE /
                  (uint32_t)surface->view_scale_y;
    dsc.pivot.x = sub_w / 2;
    dsc.pivot.y = sub_h / 2;

    coords.x1 = (w - sub_w) / 2;
    coords.y1 = (h - sub_h) / 2;
    coords.x2 = coords.x1 + sub_w - 1;
    coords.y2 = coords.y1 + sub_h - 1;
    lv_draw_image(&layer, &dsc, &coords);
    lv_canvas_finish_layer(surface->canvas, &layer);
}

/* CPU refraction samples the full screen snapshot so the edge can pull in
 * pixels outside the widget. This is the detail that gives the lens its
 * strong boundary distortion. */
static void apply_glass_effects(liquid_glass_surface_t * surface,
                                const lv_draw_buf_t * snapshot,
                                int32_t off_x,
                                int32_t off_y,
                                bool gpu_ready)
{
    lv_draw_buf_t * buf = surface->canvas_buf;
    uint8_t * data = buf->data;
    uint32_t stride = buf->header.stride;
    int32_t w = (int32_t)buf->header.w;
    int32_t h = (int32_t)buf->header.h;
    liquid_glass_params_t params = surface->params;
    lv_color_t tint_color = surface->tint_color;
    lv_opa_t tint_opa = surface_effective_tint_opa(surface);
    lv_area_t area;
    /* Stacked glass: use the strongest blur from lower overlapping layers.
     * Each surface keeps its own tint and LVGL composites the two canvas
     * images in paint order; folding the lower tint into this canvas would
     * apply it twice and turn touching glass white. */
    int32_t blur = surface_effective_blur(surface);
    if(surface->inherited_blur > blur) blur = surface->inherited_blur;
    if(surface->has_lower_overlap) {
        /* The source already contains the rendered lower lens. Keep the
         * second tint translucent instead of turning two white tints into a
         * solid white patch at the intersection. */
        tint_opa = (lv_opa_t)((uint32_t)tint_opa * 128U / 255U);
    }
    if(global_refr_height >= 0) params.refraction_height = global_refr_height;
    if(global_refr_amount >= 0) params.refraction_amount =
                                     (float)global_refr_amount / 10.0f;
    if(global_dispersion >= 0) params.dispersion =
                                    (float)global_dispersion / 10.0f;

    area.x1 = 0;
    area.y1 = 0;
    area.x2 = w - 1;
    area.y2 = h - 1;

    if(effects_enabled && snapshot != NULL) {
        bool refracted = false;

        if(gpu_ready) {
            refracted = liquid_glass_shader_process_argb8888(
                          &glass_shader,
                          snapshot->data, snapshot->header.stride,
                          (int32_t)snapshot->header.w,
                          (int32_t)snapshot->header.h,
                          off_x, off_y,
                          surface->view_scale_x, surface->view_scale_y,
                          data, stride,
                          w, h, &params);
        }
        if(!refracted) {
            liquid_glass_refract_argb8888(
              snapshot->data, snapshot->header.stride,
              (int32_t)snapshot->header.w,
              (int32_t)snapshot->header.h,
              off_x, off_y,
              data, stride,
              w, h,
              surface->view_scale_x, surface->view_scale_y,
              &params);
        }
    }

    if(blur > 0 && effects_enabled) {
        lv_layer_t layer;
        lv_draw_blur_dsc_t blur_dsc;

        lv_canvas_init_layer(surface->canvas, &layer);
        lv_draw_blur_dsc_init(&blur_dsc);
        blur_dsc.blur_radius = blur;
        blur_dsc.corner_radius = params.corner_radius;
        lv_draw_blur(&layer, &blur_dsc, &area);
        lv_canvas_finish_layer(surface->canvas, &layer);
    }

    if(tint_opa > 0 && w > LIQUID_GLASS_TINT_INSET * 2 &&
       h > LIQUID_GLASS_TINT_INSET * 2) {
        lv_layer_t layer;
        lv_draw_rect_dsc_t rect_dsc;
        lv_area_t tint_area;

        lv_canvas_init_layer(surface->canvas, &layer);
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = tint_color;
        rect_dsc.bg_opa = tint_opa;
        rect_dsc.radius = params.corner_radius > LIQUID_GLASS_TINT_INSET ?
                          params.corner_radius - LIQUID_GLASS_TINT_INSET : 0;
        tint_area.x1 = LIQUID_GLASS_TINT_INSET;
        tint_area.y1 = LIQUID_GLASS_TINT_INSET;
        tint_area.x2 = w - 1 - LIQUID_GLASS_TINT_INSET;
        tint_area.y2 = h - 1 - LIQUID_GLASS_TINT_INSET;
        lv_draw_rect(&layer, &rect_dsc, &tint_area);
        lv_canvas_finish_layer(surface->canvas, &layer);
    }
}

/* Snapshot, extract, blur, refract and tint one surface. The caller is
 * responsible for hiding the surface (and everything above it) so the
 * snapshot does not contain the surface itself. */
static void refresh_surface_content(liquid_glass_surface_t * surface,
                                    const lv_draw_buf_t * snapshot,
                                    bool gpu_ready)
{
    lv_obj_t * obj = surface->obj;
    lv_obj_t * screen = lv_obj_get_screen(obj);
    int32_t off_x;
    int32_t off_y;

    /* Positions/sizes may be stale after a move from an input event. */
    lv_obj_update_layout(obj);
    if(!ensure_canvas_geometry(surface)) return;

    if(snapshot == NULL) return;

    get_snapshot_offsets(surface, screen, snapshot, &off_x, &off_y);
    if(!effects_enabled) {
        /* Without the refraction pass nothing else fills the canvas. */
        if(surface->view_scale_x == LV_SCALE_NONE &&
           surface->view_scale_y == LV_SCALE_NONE) {
            extract_backdrop_region(surface, snapshot, off_x, off_y);
        }
        else {
            extract_backdrop_scaled(surface, snapshot, off_x, off_y);
        }
    }
    apply_glass_effects(surface, snapshot, off_x, off_y, gpu_ready);

    surface->dirty = false;
    lv_obj_invalidate(obj);
}

/* Feed a processed lower glass surface into the pass snapshot. The next
 * surface then refracts the lower result in overlap areas instead of fully
 * covering it with an independent raw-backdrop image. */
static void composite_surface_into_snapshot(
    const liquid_glass_surface_t * surface,
    lv_draw_buf_t * snapshot)
{
    int32_t snapshot_x;
    int32_t snapshot_y;
    int32_t width;
    int32_t height;
    int32_t radius;
    int32_t y;

    if(surface == NULL || snapshot == NULL || surface->canvas_buf == NULL) return;

    get_snapshot_offsets(surface,
                         lv_obj_get_screen(surface->obj),
                         snapshot,
                         &snapshot_x,
                         &snapshot_y);
    width = (int32_t)surface->canvas_buf->header.w;
    height = (int32_t)surface->canvas_buf->header.h;
    radius = surface_clip_radius(surface);
    if(radius > width / 2) radius = width / 2;
    if(radius > height / 2) radius = height / 2;

    for(y = 0; y < height; y++) {
        int32_t sy = snapshot_y + y;
        int32_t x;
        if(sy < 0 || sy >= (int32_t)snapshot->header.h) continue;

        for(x = 0; x < width; x++) {
            int32_t sx = snapshot_x + x;
            float px;
            float py;
            float qx;
            float qy;
            float outside_x;
            float outside_y;
            float distance;
            uint8_t * dst;
            const uint8_t * src;
            uint32_t alpha;

            if(sx < 0 || sx >= (int32_t)snapshot->header.w) continue;
            px = (float)x + 0.5f - (float)width * 0.5f;
            py = (float)y + 0.5f - (float)height * 0.5f;
            qx = fabsf(px) - ((float)width * 0.5f - (float)radius);
            qy = fabsf(py) - ((float)height * 0.5f - (float)radius);
            outside_x = fmaxf(qx, 0.0f);
            outside_y = fmaxf(qy, 0.0f);
            distance = sqrtf(outside_x * outside_x +
                             outside_y * outside_y) - (float)radius;
            distance += fminf(fmaxf(qx, qy), 0.0f);
            if(distance > 0.0f) continue;

            src = surface->canvas_buf->data +
                  (size_t)y * surface->canvas_buf->header.stride +
                  (size_t)x * 4U;
            dst = snapshot->data + (size_t)sy * snapshot->header.stride +
                  (size_t)sx * 4U;
            alpha = src[3];
            if(alpha >= 255U) {
                memcpy(dst, src, 4U);
            }
            else if(alpha > 0U) {
                dst[0] = (uint8_t)((src[0] * alpha + dst[0] * (255U - alpha)) / 255U);
                dst[1] = (uint8_t)((src[1] * alpha + dst[1] * (255U - alpha)) / 255U);
                dst[2] = (uint8_t)((src[2] * alpha + dst[2] * (255U - alpha)) / 255U);
            }
        }
    }
}

static lv_draw_buf_t * take_live_backdrop_snapshot(lv_obj_t * root)
{
    if(root == NULL) return NULL;

    lv_obj_update_layout(root);
    if(live_snapshot == NULL) {
        live_snapshot = lv_snapshot_create_draw_buf(
                          root, LV_COLOR_FORMAT_ARGB8888);
        if(live_snapshot == NULL) return NULL;
    }
    else if(lv_snapshot_reshape_draw_buf(root, live_snapshot) != LV_RESULT_OK) {
        lv_draw_buf_destroy(live_snapshot);
        live_snapshot = lv_snapshot_create_draw_buf(
                          root, LV_COLOR_FORMAT_ARGB8888);
        if(live_snapshot == NULL) return NULL;
    }

    lv_draw_buf_clear(live_snapshot, NULL);
    if(lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_ARGB8888,
                                    live_snapshot) != LV_RESULT_OK) {
        return NULL;
    }
    return live_snapshot;
}

/* Blend an object's local snapshot into the shared screen-sized pass buffer. */
static void overlay_object_snapshot(lv_obj_t * root,
                                    const lv_draw_buf_t * source)
{
    lv_obj_t * screen;
    lv_area_t root_coords;
    lv_area_t screen_coords;
    int32_t ext_x;
    int32_t ext_y;
    int32_t offset_x;
    int32_t offset_y;
    int32_t y;

    if(root == NULL || source == NULL || pass_snapshot == NULL) return;
    screen = lv_obj_get_screen(root);
    if(screen == NULL) return;

    lv_obj_get_coords(root, &root_coords);
    lv_obj_get_coords(screen, &screen_coords);
    ext_x = ((int32_t)source->header.w - lv_obj_get_width(root)) / 2;
    ext_y = ((int32_t)source->header.h - lv_obj_get_height(root)) / 2;
    offset_x = root_coords.x1 - screen_coords.x1 +
               ((int32_t)pass_snapshot->header.w -
                lv_obj_get_width(screen)) / 2 - ext_x;
    offset_y = root_coords.y1 - screen_coords.y1 +
               ((int32_t)pass_snapshot->header.h -
                lv_obj_get_height(screen)) / 2 - ext_y;

    for(y = 0; y < (int32_t)source->header.h; y++) {
        int32_t dst_y = offset_y + y;
        int32_t x;

        if(dst_y < 0 || dst_y >= (int32_t)pass_snapshot->header.h) continue;
        for(x = 0; x < (int32_t)source->header.w; x++) {
            int32_t dst_x = offset_x + x;
            const uint8_t * src;
            uint8_t * dst;
            uint32_t alpha;

            if(dst_x < 0 || dst_x >= (int32_t)pass_snapshot->header.w) continue;
            src = source->data + (size_t)y * source->header.stride +
                  (size_t)x * 4U;
            alpha = src[3];
            if(alpha == 0) continue;

            dst = pass_snapshot->data +
                  (size_t)dst_y * pass_snapshot->header.stride +
                  (size_t)dst_x * 4U;
            if(alpha >= 255U) {
                memcpy(dst, src, 4U);
            }
            else {
                uint32_t inverse_alpha = 255U - alpha;

                dst[0] = (uint8_t)((src[0] * alpha +
                                    dst[0] * inverse_alpha) / 255U);
                dst[1] = (uint8_t)((src[1] * alpha +
                                    dst[1] * inverse_alpha) / 255U);
                dst[2] = (uint8_t)((src[2] * alpha +
                                    dst[2] * inverse_alpha) / 255U);
            }
        }
    }
}

static lv_obj_tree_walk_res_t collect_walk_cb(lv_obj_t * obj, void * user_data)
{
    liquid_glass_collect_ctx_t * ctx = (liquid_glass_collect_ctx_t *)user_data;
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface != NULL && ctx->count < ctx->capacity) {
        ctx->order[ctx->count++] = surface;
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

/* A surface whose ancestor is hidden (e.g. tiles inside a hidden sheet)
 * is invisible for rendering purposes and must not be refreshed. */
static bool is_effectively_hidden(const lv_obj_t * obj)
{
    while(obj != NULL) {
        if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return true;
        obj = lv_obj_get_parent(obj);
    }
    return false;
}

/* Refresh one surface against the shared pass snapshot, then mark the
 * surfaces above it dirty when their backdrop region overlaps (their
 * sample region moved with it). */
static void refresh_at(liquid_glass_surface_t ** order,
                       uint32_t count,
                       uint32_t index)
{
    liquid_glass_surface_t * surface = order[index];
    const lv_draw_buf_t * backdrop = pass_snapshot;
    bool gpu_ready = pass_gpu_ready;
    lv_area_t a;
    lv_area_t b;
    uint32_t j;

    if(is_effectively_hidden(surface->hide_target)) return;

    if(surface->live_backdrop) {
        lv_obj_t * root = lv_obj_get_parent(surface->obj);
        const lv_draw_buf_t * live;

        if(root == NULL) root = lv_screen_active();
        for(j = index; j < count; j++) {
            lv_obj_t * target = order[j]->hide_target;

            order[j]->pass_hidden = target != NULL &&
                !lv_obj_has_flag(target, LV_OBJ_FLAG_HIDDEN);
            if(order[j]->pass_hidden) {
                lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
            }
        }
        live = take_live_backdrop_snapshot(root);
        for(j = index; j < count; j++) {
            if(order[j]->pass_hidden) {
                lv_obj_clear_flag(order[j]->hide_target,
                                  LV_OBJ_FLAG_HIDDEN);
            }
        }
        if(live != NULL) overlay_object_snapshot(root, live);
        backdrop = pass_snapshot;
        gpu_ready = false;
    }

    lv_obj_get_coords(surface->obj, &a);

    /* Keep the strongest frost from overlapping lower layers. Their rendered
     * pixels are also already present in pass_snapshot at this point, so the
     * current surface gets both the lower blur and the lower refraction. */
    {
        liquid_glass_surface_t * s = surface;

        s->inherited_blur = 0;
        s->has_lower_overlap = false;
        for(j = 0; !s->live_backdrop && j < index; j++) {
            int32_t blur_below;

            if(is_effectively_hidden(order[j]->hide_target)) continue;
            lv_obj_get_coords(order[j]->obj, &b);
            if(!(a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1)) {
                continue;
            }
            s->has_lower_overlap = true;
            blur_below = surface_effective_blur(order[j]);
            if(blur_below > s->inherited_blur) {
                s->inherited_blur = blur_below;
            }
        }
    }

    refresh_surface_content(surface, backdrop, gpu_ready);
    composite_surface_into_snapshot(surface, pass_snapshot);

    /* Surfaces above this one moved with it (children) or overlap it, so
     * their sample region changed; surfaces below can be left alone. */
    for(j = index + 1; j < count; j++) {
        lv_area_t b;

        lv_obj_get_coords(order[j]->obj, &b);
        if(a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1) {
            order[j]->dirty = true;
        }
    }
}

/* Refresh glass surfaces on the active screen in back-to-front order.
 * With `only_obj` only that surface is refreshed (still with the surfaces
 * above it hidden during the snapshot); otherwise all surfaces, or only the
 * dirty ones when `dirty_only` is set. */
static void refresh_stack(lv_obj_t * only_obj, bool dirty_only)
{
    liquid_glass_surface_t * iter;
    liquid_glass_surface_t ** order;
    uint32_t count = 0;
    uint32_t n;
    uint32_t i;
    uint32_t target_index = UINT32_MAX;
    void * mem;

    for(iter = surface_list; iter != NULL; iter = iter->next) count++;
    if(count == 0) return;
    if(lv_display_get_default() == NULL) return;   /* display torn down */
    if(lv_screen_active() == NULL) return;         /* screens torn down */

    mem = lv_malloc(count * sizeof(liquid_glass_surface_t *));
    if(mem == NULL) return;
    order = (liquid_glass_surface_t **)mem;

    {
        liquid_glass_collect_ctx_t ctx;

        ctx.order = order;
        ctx.count = 0;
        ctx.capacity = count;
        /* Paint order (depth first, children by index) is back to front. */
        lv_obj_tree_walk(lv_screen_active(), collect_walk_cb, &ctx);
        n = ctx.count;
    }

    if(only_obj != NULL) {
        for(i = 0; i < n; i++) {
            if(order[i]->obj == only_obj) {
                target_index = i;
                break;
            }
        }
        if(target_index == UINT32_MAX) {
            lv_free(mem);
            return;
        }
    }

    /* ONE raw snapshot for the whole pass, taken with every glass surface
     * hidden. Processed lower canvases are fed back into this buffer as the
     * back-to-front walk advances, so upper surfaces see the same layered
     * backdrop that LVGL will paint. */
    for(iter = surface_list; iter != NULL; iter = iter->next) {
        iter->pass_hidden = !lv_obj_has_flag(iter->hide_target,
                                             LV_OBJ_FLAG_HIDDEN);
        if(iter->pass_hidden) {
            lv_obj_add_flag(iter->hide_target, LV_OBJ_FLAG_HIDDEN);
        }
    }
    pass_snapshot = take_screen_snapshot(lv_screen_active());
    for(iter = surface_list; iter != NULL; iter = iter->next) {
        if(iter->pass_hidden) {
            lv_obj_clear_flag(iter->hide_target, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* One screen texture upload per pass for the GPU backend. */
    pass_gpu_ready = false;
    if(pass_snapshot != NULL && render_mode != LIQUID_GLASS_RENDER_CPU &&
       shader_ready) {
        pass_gpu_ready = liquid_glass_shader_upload_screen(
          &glass_shader,
          pass_snapshot->data, pass_snapshot->header.stride,
          (int32_t)pass_snapshot->header.w,
          (int32_t)pass_snapshot->header.h);
    }

    for(i = 0; i < n; i++) {
        bool refresh_this;

        if(only_obj != NULL) {
            /* A single surface still depends on every lower surface. Reuse
             * their cached canvases, but refresh a lower one if it is dirty. */
            refresh_this = i < target_index ? order[i]->dirty :
                           i == target_index;
            if(i > target_index) continue;
        }
        else {
            refresh_this = !dirty_only || order[i]->dirty;
        }

        if(refresh_this) {
            refresh_at(order, n, i);
        }
        else if(!is_effectively_hidden(order[i]->hide_target)) {
            /* pass_snapshot starts from the raw backdrop on every pass, so
             * unchanged lower layers must still be fed into it before an
             * upper layer samples the region. */
            composite_surface_into_snapshot(order[i], pass_snapshot);
        }
    }

    pass_snapshot = NULL;
    lv_free(mem);
}

/* Unlink the surface from the registry and release everything it owns.
 * Never touches the object tree, so it is safe from LV_EVENT_DELETE. */
static void detach_surface(liquid_glass_surface_t * surface)
{
    liquid_glass_surface_t ** link = &surface_list;

    while(*link != NULL) {
        if(*link == surface) {
            *link = surface->next;
            break;
        }
        link = &(*link)->next;
    }

    lv_draw_buf_destroy(surface->canvas_buf);
    lv_free(surface);
}

static void glass_event_cb(lv_event_t * e)
{
    liquid_glass_surface_t * surface = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if(surface == NULL) return;

    if(code == LV_EVENT_DELETE) {
        /* The widget and its canvas child are going away with the tree;
         * only the surface and its buffers need to be released here. */
        detach_surface(surface);
        return;
    }
    if(code == LV_EVENT_SIZE_CHANGED || code == LV_EVENT_STYLE_CHANGED) {
        surface->dirty = true;
    }
}

static void manager_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(surface_list == NULL) return;
    if(refresh_policy == LIQUID_GLASS_REFRESH_MANUAL) return;
    if(lv_display_get_default() == NULL) return;   /* display torn down */
    if(lv_screen_active() == NULL) return;         /* screens torn down */

    refresh_stack(NULL, refresh_policy == LIQUID_GLASS_REFRESH_DIRTY);
}

static void ensure_manager(void)
{
    if(manager_timer != NULL) return;
    manager_timer = lv_timer_create(manager_timer_cb, refresh_period, NULL);
}

bool liquid_glass_apply(lv_obj_t * obj, const liquid_glass_params_t * params)
{
    liquid_glass_surface_t * surface;
    liquid_glass_surface_t ** link;

    if(obj == NULL || find_surface(obj) != NULL) return false;

    surface = lv_malloc_zeroed(sizeof(*surface));
    if(surface == NULL) return false;

    surface->obj = obj;
    surface->hide_target = obj;
    surface->blur_radius = 2;
    surface->view_scale_x = LV_SCALE_NONE;
    surface->view_scale_y = LV_SCALE_NONE;
    surface->tint_color = lv_color_white();
    surface->tint_opa = LV_OPA_TRANSP;
    surface->dirty = true;
    if(params != NULL) surface->params = *params;
    else liquid_glass_params_init(&surface->params);

    surface->canvas = lv_canvas_create(obj);
    if(surface->canvas == NULL) {
        lv_free(surface);
        return false;
    }
    /* The canvas never renders itself; only its image is used as the
     * widget's background. */
    lv_obj_add_flag(surface->canvas, LV_OBJ_FLAG_HIDDEN);

    /* Plain registry append; the refresh order comes from the object tree
     * walk, not from this list. */
    link = &surface_list;
    while(*link != NULL) link = &(*link)->next;
    *link = surface;

    lv_obj_update_layout(obj);
    ensure_canvas_geometry(surface);
    lv_obj_set_style_clip_corner(obj, true, 0);

    lv_obj_add_event_cb(obj, glass_event_cb, LV_EVENT_ALL, surface);

    ensure_manager();
    return true;
}

bool liquid_glass_remove(lv_obj_t * obj)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface == NULL) return false;

    lv_obj_remove_event_cb(obj, glass_event_cb);
    lv_obj_set_style_bg_image_src(obj, NULL, 0);
    if(lv_obj_is_in_widget_tree(surface->canvas)) {
        lv_obj_delete(surface->canvas);
    }
    detach_surface(surface);
    return true;
}

bool liquid_glass_is_applied(const lv_obj_t * obj)
{
    return find_surface(obj) != NULL;
}

lv_obj_t * liquid_glass_create(lv_obj_t * parent, int32_t width, int32_t height)
{
    lv_obj_t * obj = lv_obj_create(parent);
    liquid_glass_params_t params;

    if(obj == NULL) return NULL;
    liquid_glass_params_init(&params);

    lv_obj_set_size(obj, width, height);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, params.corner_radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);

    liquid_glass_apply(obj, &params);
    return obj;
}

liquid_glass_params_t * liquid_glass_get_params(lv_obj_t * obj)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    return surface != NULL ? &surface->params : NULL;
}

void liquid_glass_set_blur(lv_obj_t * obj, int32_t blur_radius)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface == NULL || surface->blur_radius == blur_radius) return;
    surface->blur_radius = blur_radius;
    surface->dirty = true;
}

void liquid_glass_set_tint(lv_obj_t * obj, lv_color_t color, lv_opa_t opa)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface == NULL) return;
    if(lv_color_eq(surface->tint_color, color) && surface->tint_opa == opa) return;
    surface->tint_color = color;
    surface->tint_opa = opa;
    surface->dirty = true;
}

void liquid_glass_refresh(lv_obj_t * obj)
{
    if(find_surface(obj) == NULL) return;
    if(lv_obj_get_screen(obj) != lv_screen_active()) return;

    refresh_stack(obj, false);
}

void liquid_glass_refresh_all(void)
{
    refresh_stack(NULL, false);
}

void liquid_glass_mark_dirty(lv_obj_t * obj)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface != NULL) surface->dirty = true;
}

void liquid_glass_set_hide_target(lv_obj_t * glass, lv_obj_t * target)
{
    liquid_glass_surface_t * surface = find_surface(glass);

    if(surface == NULL) return;
    surface->hide_target = target != NULL ? target : glass;
}

void liquid_glass_set_live_backdrop(lv_obj_t * obj, bool enabled)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface == NULL || surface->live_backdrop == enabled) return;
    surface->live_backdrop = enabled;
    surface->dirty = true;
}

void liquid_glass_set_view_scale_xy(lv_obj_t * obj, int32_t scale_x,
                                    int32_t scale_y)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    if(surface == NULL || scale_x <= 0 || scale_y <= 0) return;
    if(surface->view_scale_x == scale_x &&
       surface->view_scale_y == scale_y) return;
    surface->view_scale_x = scale_x;
    surface->view_scale_y = scale_y;
    surface->dirty = true;
}

void liquid_glass_set_enabled(bool enabled)
{
    if(effects_enabled == enabled) return;
    effects_enabled = enabled;
    mark_all_dirty();
}

bool liquid_glass_is_enabled(void)
{
    return effects_enabled;
}

void liquid_glass_set_global_blur(int32_t blur_radius)
{
    if(global_blur == blur_radius) return;
    global_blur = blur_radius;
    mark_all_dirty();
}

void liquid_glass_set_global_refraction(int32_t height, int32_t amount_x10)
{
    if(global_refr_height != height) {
        global_refr_height = height;
        mark_all_dirty();
    }
    if(global_refr_amount != amount_x10) {
        global_refr_amount = amount_x10;
        mark_all_dirty();
    }
}

void liquid_glass_set_global_dispersion(int32_t dispersion_x10)
{
    if(global_dispersion == dispersion_x10) return;
    global_dispersion = dispersion_x10;
    mark_all_dirty();
}

void liquid_glass_set_global_tint_opa(int32_t opa)
{
    if(global_tint_opa == opa) return;
    global_tint_opa = opa;
    mark_all_dirty();
}

int32_t liquid_glass_get_global_blur(void)
{
    return global_blur;
}

int32_t liquid_glass_get_global_refraction_height(void)
{
    return global_refr_height;
}

int32_t liquid_glass_get_global_refraction_amount_x10(void)
{
    return global_refr_amount;
}

int32_t liquid_glass_get_global_dispersion_x10(void)
{
    return global_dispersion;
}

int32_t liquid_glass_get_global_tint_opa(void)
{
    return global_tint_opa;
}

const lv_draw_buf_t * liquid_glass_debug_canvas_buf(const lv_obj_t * obj)
{
    liquid_glass_surface_t * surface = find_surface(obj);

    return surface != NULL ? surface->canvas_buf : NULL;
}

void liquid_glass_set_refresh_policy(liquid_glass_refresh_policy_t policy)
{
    refresh_policy = policy;
}

liquid_glass_refresh_policy_t liquid_glass_get_refresh_policy(void)
{
    return refresh_policy;
}

void liquid_glass_set_refresh_period(uint32_t period_ms)
{
    if(period_ms == 0) period_ms = 1;
    refresh_period = period_ms;
    if(manager_timer != NULL) lv_timer_set_period(manager_timer, period_ms);
}

void liquid_glass_set_render_mode(liquid_glass_render_mode_t mode)
{
    render_mode = mode;
    if(mode == LIQUID_GLASS_RENDER_CPU || shader_ready || shader_failed) return;

    /* Try to bring the GPU backend up once; stay on the CPU for good if it
     * is unavailable. */
    if(liquid_glass_shader_init(&glass_shader)) {
        shader_ready = true;
    }
    else {
        shader_failed = true;
    }
}

liquid_glass_render_mode_t liquid_glass_get_render_mode(void)
{
    return render_mode;
}

bool liquid_glass_using_shader(void)
{
    return render_mode != LIQUID_GLASS_RENDER_CPU && shader_ready;
}
