// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/paradise.h>
#include <hid/usages.h>
#include <lib/framebuffer/framebuffer.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/input.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define DEV_INPUT "/dev/class/input"
#define SPRITE_DIMEN 100
#define NUM_SPRITES 5

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, min, max) MIN(MAX(x, min), max)

typedef struct scene {
    struct {
        int64_t x;
        int64_t y;
    } sprites[NUM_SPRITES];
    struct {
        uint32_t x;
        uint32_t y;
    } pen;
} scene_t;

static uint32_t scale(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim) {
    return (z * screen_dim) / rpt_dim;
}

int main(int argc, char* argv[]) {
    const char* err;
    zx_status_t status = fb_bind(true, &err);
    if (status != ZX_OK) {
        printf("failed to open framebuffer: %d (%s)\n", status, err);
        return -1;
    }

    uint32_t width;
    uint32_t height;
    uint32_t stride;
    zx_pixel_format_t format;
    fb_get_config(&width, &height, &stride, &format);

    zx_handle_t vmo = fb_get_single_buffer();
    size_t size = ZX_PIXEL_FORMAT_BYTES(format) * height * stride;
    uintptr_t data;
    status = _zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                          ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                          &data);
    if (status < 0) {
        printf("couldn't map vmo: %d\n", status);
        return -1;
    }

    uint8_t* pixels = (uint8_t*)data;
    gfx_surface* surface = gfx_create_surface((void*)data,
                                              width,
                                              height,
                                              stride,
                                              format,
                                              GFX_FLAG_FLUSH_CPU_CACHE);
    assert(surface);
    gfx_clear(surface, 0xffffffff);

    DIR* dir = opendir(DEV_INPUT);
    if (!dir) {
        printf("failed to open %s: %d\n", DEV_INPUT, errno);
        return -1;
    }

    ssize_t ret;
    int touchfd = -1;
    int touchpadfd = -1;
    struct dirent* de;
    while ((de = readdir(dir))) {
        char devname[128];

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        snprintf(devname, sizeof(devname), "%s/%s", DEV_INPUT, de->d_name);
        int fd = open(devname, O_RDONLY);
        if (fd < 0) {
            printf("failed to open %s: %d\n", devname, errno);
            continue;
        }

        size_t rpt_desc_len = 0;
        ret = ioctl_input_get_report_desc_size(fd, &rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor length for %s: %zd\n",
                   devname,
                   ret);
            close(fd);
            continue;
        }

        uint8_t rpt_desc[rpt_desc_len];
        ret = ioctl_input_get_report_desc(fd, rpt_desc, rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor for %s: %zd\n",
                   devname,
                   ret);
            close(fd);
            continue;
        }

        if (is_paradise_touch_v3_report_desc(rpt_desc, rpt_desc_len)) {
            touchfd = fd;
            continue;
        }

        if (is_paradise_touchpad_v2_report_desc(rpt_desc, rpt_desc_len)) {
            touchpadfd = fd;
            continue;
        }

        close(fd);
    }
    closedir(dir);

    if (touchfd < 0 && touchpadfd < 0) {
        printf("could not find a touch device\n");
        return -1;
    }

    input_report_size_t max_rpt_sz = 0;
    input_report_size_t max_touch_rpt_sz = 0;
    if (touchfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchfd, &max_touch_rpt_sz);
        if (ret < 0) {
            printf("failed to get max report size: %zd\n", ret);
            return -1;
        }
        max_rpt_sz = max_touch_rpt_sz;
    }

    input_report_size_t max_touchpad_rpt_sz = 0;
    if (touchpadfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchpadfd, &max_touchpad_rpt_sz);
        if (ret < 0) {
            printf("failed to get max report size: %zd\n", ret);
            return -1;
        }
        if (max_touchpad_rpt_sz > max_rpt_sz)
            max_rpt_sz = max_touchpad_rpt_sz;
    }

    void* rpt_buf = malloc(max_rpt_sz);
    assert(rpt_buf);

    uint32_t colors[] = {
        0x00ff0000,
        0x0000ff00,
        0x000000ff,
        0x00ffff00,
        0x00ff00ff,
    };
    scene_t current_scene = {
        .sprites = {
            {.x = INT_MAX, .y = INT_MAX},
            {.x = INT_MAX, .y = INT_MAX},
            {.x = INT_MAX, .y = INT_MAX},
            {.x = INT_MAX, .y = INT_MAX},
            {.x = INT_MAX, .y = INT_MAX}},
        .pen = {.x = INT_MAX, .y = INT_MAX}};
    scene_t pending_scene = current_scene;
    int timeout = -1;

    while (true) {
        int startfd = 1;
        int endfd = 1;
        struct pollfd fds[2];

        if (touchfd >= 0) {
            fds[0].fd = touchfd;
            fds[0].events = POLLIN;
            fds[0].revents = 0;
            startfd = 0;
        }
        if (touchpadfd >= 0) {
            fds[1].fd = touchpadfd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            endfd = 2;
        }

        int ready = poll(&fds[startfd], endfd - startfd, timeout);
        if (ready) {
            if (touchfd >= 0 && fds[0].revents) {
                ssize_t bytes = read(touchfd, rpt_buf, max_touch_rpt_sz);
                if (bytes < 0) {
                    printf("read error: %zd (errno=%d)\n", bytes, errno);
                    break;
                }

                uint8_t id = *(uint8_t*)rpt_buf;
                if (id == PARADISE_RPT_ID_TOUCH) {
                    paradise_touch_t* rpt = (paradise_touch_t*)rpt_buf;

                    for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                        if (paradise_finger_flags_tswitch(rpt->fingers[c].flags)) {
                            pending_scene.sprites[c].x =
                                scale(rpt->fingers[c].x, width, PARADISE_X_MAX);
                            pending_scene.sprites[c].y =
                                scale(rpt->fingers[c].y, height, PARADISE_Y_MAX);
                        } else {
                            pending_scene.sprites[c].x = INT_MAX;
                            pending_scene.sprites[c].y = INT_MAX;
                        }
                    }
                } else if (id == PARADISE_RPT_ID_STYLUS) {
                    paradise_stylus_t* rpt = (paradise_stylus_t*)rpt_buf;

                    if (paradise_stylus_status_tswitch(rpt->status)) {
                        pending_scene.pen.x = scale(rpt->x, width, PARADISE_STYLUS_X_MAX);
                        pending_scene.pen.y = scale(rpt->y, height, PARADISE_STYLUS_Y_MAX);
                    } else {
                        pending_scene.pen.x = INT_MAX;
                        pending_scene.pen.y = INT_MAX;
                    }
                }
            }
            if (touchpadfd >= 0 && fds[1].revents) {
                ssize_t bytes = read(touchpadfd, rpt_buf, max_touchpad_rpt_sz);
                if (bytes < 0) {
                    printf("read error: %zd (errno=%d)\n", bytes, errno);
                    break;
                }

                paradise_touchpad_t* rpt = (paradise_touchpad_t*)rpt_buf;

                for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                    if (rpt->fingers[c].tip_switch) {
                        pending_scene.sprites[c].x =
                            scale(rpt->fingers[c].x, width, PARADISE_X_MAX);
                        pending_scene.sprites[c].y =
                            scale(rpt->fingers[c].y, height, PARADISE_Y_MAX);
                    } else {
                        pending_scene.sprites[c].x = INT_MAX;
                        pending_scene.sprites[c].y = INT_MAX;
                    }
                }
            }
        }

        // Wait forever for new events if scene hasn't changed.
        if (!memcmp(&pending_scene, &current_scene, sizeof(current_scene))) {
            timeout = -1;
            continue;
        }

        // Defer scene update until all pending events have been processed.
        if (timeout) {
            timeout = 0;
            continue;
        }

        // Update line.
        if (memcmp(&pending_scene.pen,
                   &current_scene.pen,
                   sizeof(current_scene.pen)) &&
            current_scene.pen.x != INT_MAX &&
            current_scene.pen.y != INT_MAX &&
            pending_scene.pen.x != INT_MAX &&
            pending_scene.pen.y != INT_MAX) {
            int64_t xmin = MIN(current_scene.pen.x, pending_scene.pen.x);
            int64_t xmax = MAX(current_scene.pen.x, pending_scene.pen.x);
            int64_t ymin = MIN(current_scene.pen.y, pending_scene.pen.y);
            int64_t ymax = MAX(current_scene.pen.y, pending_scene.pen.y);

            xmin = CLAMP(xmin, 0, width);
            xmax = CLAMP(xmax + 1, 0, width);
            ymin = CLAMP(ymin, 0, height);
            ymax = CLAMP(ymax + 1, 0, height);

            gfx_line(surface,
                     current_scene.pen.x,
                     current_scene.pen.y,
                     pending_scene.pen.x,
                     pending_scene.pen.y,
                     /*color=*/0);

            if (xmin < xmax && ymin < ymax) {
                for (int64_t y = ymin; y < ymax; ++y) {
                    uint8_t* p = pixels + y * surface->stride * surface->pixelsize;

                    zx_cache_flush(p + xmin * surface->pixelsize,
                                   (xmax - xmin) * surface->pixelsize,
                                   ZX_CACHE_FLUSH_DATA);
                }
            }
        }

        // Update sprites.
        if (memcmp(&pending_scene.sprites,
                   &current_scene.sprites,
                   sizeof(current_scene.sprites))) {
            // Note: Update buffer by iterating over each line. This prevents
            // flicker when drawing to a single buffer.
            for (int64_t y = 0; y < height; ++y) {
                int64_t xmin = width;
                int64_t xmax = 0;

                // Determine if any of our sprites intersect this line and
                // potentially needs to be updated. We redraw all sprites each
                // time one of the sprites changed.
                for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                    int64_t y1, y2;

                    y1 = pending_scene.sprites[c].y - SPRITE_DIMEN;
                    y2 = pending_scene.sprites[c].y + SPRITE_DIMEN;
                    if (y1 < y && y2 > y) {
                        xmin = MIN(xmin, pending_scene.sprites[c].x - SPRITE_DIMEN);
                        xmax = MAX(xmax, pending_scene.sprites[c].x + SPRITE_DIMEN);
                    }
                    y1 = current_scene.sprites[c].y - SPRITE_DIMEN;
                    y2 = current_scene.sprites[c].y + SPRITE_DIMEN;
                    if (y1 < y && y2 > y) {
                        xmin = MIN(xmin, current_scene.sprites[c].x - SPRITE_DIMEN);
                        xmax = MAX(xmax, current_scene.sprites[c].x + SPRITE_DIMEN);
                    }
                }

                xmin = CLAMP(xmin, 0, width);
                xmax = CLAMP(xmax, 0, width);

                if (xmin < xmax) {
                    uint8_t* p = pixels + y * surface->stride * surface->pixelsize;
                    int64_t color_start_x = xmin;
                    uint32_t color = 0xffffffff;

                    // Note: Instead of clearing and redrawing each sprite, we
                    // update the buffer by iterating over each line. This
                    // prevents flicker when drawing in single buffer mode and
                    // minimizes the number of bytes that needs to be written.
                    for (int64_t x = xmin; x < xmax; ++x) {
                        uint32_t new_color = 0xffffffff;
                        for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                            int64_t x1 = pending_scene.sprites[c].x - SPRITE_DIMEN;
                            int64_t x2 = pending_scene.sprites[c].x + SPRITE_DIMEN;
                            int64_t y1 = pending_scene.sprites[c].y - SPRITE_DIMEN;
                            int64_t y2 = pending_scene.sprites[c].y + SPRITE_DIMEN;

                            if (x1 < x && x2 > x && y1 < y && y2 > y) {
                                new_color = colors[c];
                                break;
                            }
                        }

                        // If color is changing, write span with old color.
                        if (new_color != color) {
                            gfx_fillrect(surface,
                                         (uint32_t)color_start_x,
                                         (uint32_t)y,
                                         (uint32_t)(x - color_start_x),
                                         /*height=*/1,
                                         color);
                            color_start_x = x;
                            color = new_color;
                        }
                    }

                    // Write span at the end of line.
                    if (color_start_x < xmax) {
                        gfx_fillrect(surface,
                                     (uint32_t)color_start_x,
                                     (uint32_t)y,
                                     (uint32_t)(xmax - color_start_x),
                                     /*height=*/1,
                                     color);
                    }

                    zx_cache_flush(p + xmin * surface->pixelsize,
                                   (xmax - xmin) * surface->pixelsize,
                                   ZX_CACHE_FLUSH_DATA);
                }
            }
        }

        current_scene = pending_scene;
        timeout = -1;
    }

    free(rpt_buf);
    if (touchfd >= 0)
        close(touchfd);
    if (touchpadfd >= 0)
        close(touchpadfd);
    gfx_surface_destroy(surface);
    _zx_vmar_unmap(zx_vmar_root_self(), data, size);
    fb_release();
    return 0;
}
