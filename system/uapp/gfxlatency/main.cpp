// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/paradise.h>
#include <hid/usages.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/framebuffer/framebuffer.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <unistd.h>
#include <zircon/device/input.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define DEV_INPUT "/dev/class/input"
#define SPRITE_DIMEN 100
#define NUM_SPRITES 5
#define NUM_BUFFERS 2

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, min, max) MIN(MAX(x, min), max)

enum class VSync {
    ON,
    OFF,
    ADAPTIVE,
};

typedef struct point {
    uint32_t x;
    uint32_t y;
} point_t;

typedef struct line {
    point_t p0;
    point_t p1;
} line_t;

typedef struct sprite {
    int64_t x;
    int64_t y;
} sprite_t;

typedef struct buffer {
    zx_handle_t vmo;
    uintptr_t data;
    uint64_t image_id;
    gfx_surface* surface;
    zx_handle_t wait_event;
    uint64_t wait_event_id;
    sprite_t sprites[NUM_SPRITES];
    fbl::Vector<line_t> pending_lines;
} buffer_t;

static uint32_t scale(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim) {
    return (z * screen_dim) / rpt_dim;
}

static void prepare_poll(int touchfd,
                         int touchpadfd,
                         int* startfd,
                         int* endfd,
                         struct pollfd* fds) {
    *startfd = 1;
    *endfd = 1;
    if (touchfd >= 0) {
        fds[0].fd = touchfd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        *startfd = 0;
    }
    if (touchpadfd >= 0) {
        fds[1].fd = touchpadfd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        *endfd = 2;
    }
}

static void print_usage(FILE* stream) {
    fprintf(stream,
            "usage: gfxlatency [options]\n\n"
            "options:\n"
            "  -h, --help\t\t\tPrint this help\n"
            "  --vsync=on|off|adaptive\tVSync mode (default=adaptive)\n"
            "  --offset=MS\t\t\tVSync offset (default=15)\n");
}

int main(int argc, char* argv[]) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider provider(loop.dispatcher());

    async::Loop update_loop(&kAsyncLoopConfigNoAttachToThread);
    update_loop.StartThread();

    VSync vsync = VSync::ADAPTIVE;
    zx_time_t vsync_offset = ZX_MSEC(15);

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strstr(arg, "--vsync") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            if (!strcmp(s, "on")) {
                vsync = VSync::ON;
            } else if (!strcmp(s, "off")) {
                vsync = VSync::OFF;
            } else if (!strcmp(s, "adaptive")) {
                vsync = VSync::ADAPTIVE;
            } else {
                fprintf(stderr, "invalid vsync mode: %s\n", s);
                print_usage(stderr);
                return -1;
            }
        } else if (strstr(arg, "--offset") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            vsync_offset = ZX_MSEC(atoi(s));
        } else if (strstr(arg, "-h") == arg) {
            print_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "invalid argument: %s\n", arg);
            print_usage(stderr);
            return -1;
        }
    }

    const char* err;
    zx_status_t status = fb_bind(false, &err);
    if (status != ZX_OK) {
        fprintf(stderr, "failed to open framebuffer: %d (%s)\n", status, err);
        return -1;
    }

    uint32_t width;
    uint32_t height;
    uint32_t stride;
    zx_pixel_format_t format;
    fb_get_config(&width, &height, &stride, &format);
    size_t buffer_size = ZX_PIXEL_FORMAT_BYTES(format) * height * stride;

    sprite_t sprites[NUM_SPRITES] = {
        {.x = INT_MAX, .y = INT_MAX},
        {.x = INT_MAX, .y = INT_MAX},
        {.x = INT_MAX, .y = INT_MAX},
        {.x = INT_MAX, .y = INT_MAX},
        {.x = INT_MAX, .y = INT_MAX}};
    point_t pen = {.x = INT_MAX, .y = INT_MAX};

    // Initialize buffers and associated state.
    buffer_t buffer_storage[NUM_BUFFERS];
    fbl::Array<buffer_t> buffers(buffer_storage, vsync == VSync::OFF ? 1 : NUM_BUFFERS);
    uint64_t next_event_id = FB_INVALID_ID + 1;
    for (auto& buffer : buffers) {
        memcpy(buffer.sprites, sprites, sizeof(sprites));

        status = fb_alloc_image_buffer(&buffer.vmo);
        ZX_ASSERT(status == ZX_OK);

        zx_vmo_set_cache_policy(buffer.vmo, ZX_CACHE_POLICY_WRITE_COMBINING);

        zx_handle_t dup;
        status = zx_handle_duplicate(buffer.vmo, ZX_RIGHT_SAME_RIGHTS, &dup);
        ZX_ASSERT(status == ZX_OK);

        status = fb_import_image(dup, 0, &buffer.image_id);
        ZX_ASSERT(status == ZX_OK);

        status = zx_vmar_map(zx_vmar_root_self(), 0, buffer.vmo, 0, buffer_size,
                             ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                             &buffer.data);
        ZX_ASSERT(status == ZX_OK);

        buffer.surface = gfx_create_surface((void*)buffer.data,
                                            width,
                                            height,
                                            stride,
                                            format,
                                            GFX_FLAG_FLUSH_CPU_CACHE);
        ZX_ASSERT(buffer.surface);
        gfx_clear(buffer.surface, 0xffffffff);

        switch (vsync) {
        case VSync::ON:
            status = zx_event_create(0, &buffer.wait_event);
            ZX_ASSERT(status == ZX_OK);
            status = zx_handle_duplicate(buffer.wait_event, ZX_RIGHT_SAME_RIGHTS, &dup);
            ZX_ASSERT(status == ZX_OK);
            buffer.wait_event_id = next_event_id++;
            status = fb_import_event(dup, buffer.wait_event_id);
            ZX_ASSERT(status == ZX_OK);
            break;
        case VSync::ADAPTIVE:
        case VSync::OFF:
            buffer.wait_event = ZX_HANDLE_INVALID;
            buffer.wait_event_id = FB_INVALID_ID;
            break;
        }
    }

    // Enable vsync if needed.
    switch (vsync) {
    case VSync::ON:
    case VSync::ADAPTIVE:
        if ((status = fb_enable_vsync(true)) != ZX_OK) {
            fprintf(stderr, "failed to enable vsync\n");
            return -1;
        }
        break;
    case VSync::OFF:
        break;
    }

    // Present initial buffer.
    if ((status = fb_present_image2(buffers[0].image_id,
                                    FB_INVALID_ID,
                                    FB_INVALID_ID)) != ZX_OK) {
        fprintf(stderr, "failed to present framebuffer\n");
        return -1;
    }

    DIR* dir = opendir(DEV_INPUT);
    if (!dir) {
        fprintf(stderr, "failed to open %s: %d\n", DEV_INPUT, errno);
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
            fprintf(stderr, "failed to open %s: %d\n", devname, errno);
            continue;
        }

        size_t rpt_desc_len = 0;
        ret = ioctl_input_get_report_desc_size(fd, &rpt_desc_len);
        if (ret < 0) {
            fprintf(stderr, "failed to get report descriptor length for %s: %zd\n",
                    devname,
                    ret);
            close(fd);
            continue;
        }

        uint8_t rpt_desc[rpt_desc_len];
        ret = ioctl_input_get_report_desc(fd, rpt_desc, rpt_desc_len);
        if (ret < 0) {
            fprintf(stderr, "failed to get report descriptor for %s: %zd\n",
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
        fprintf(stderr, "could not find a touch device\n");
        return -1;
    }

    input_report_size_t max_touch_rpt_sz = 0;
    if (touchfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchfd, &max_touch_rpt_sz);
        ZX_ASSERT(ret >= 0);
    }
    input_report_size_t max_touchpad_rpt_sz = 0;
    if (touchpadfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchpadfd, &max_touchpad_rpt_sz);
        ZX_ASSERT(ret >= 0);
    }

    size_t update = 1;
    async::TaskClosure update_task([&width,
                                    &height,
                                    &vsync,
                                    &buffers,
                                    &sprites,
                                    &pen,
                                    &touchfd,
                                    &touchpadfd,
                                    &max_touch_rpt_sz,
                                    &max_touchpad_rpt_sz,
                                    &update] {
        // Process all pending input events.
        int ready = 0;
        do {
            TRACE_DURATION("app", "ProcessInputEvents");

            int startfd, endfd;
            struct pollfd fds[2];

            prepare_poll(touchfd, touchpadfd, &startfd, &endfd, fds);
            ready = poll(&fds[startfd], endfd - startfd, 0);
            if (touchfd >= 0 && fds[0].revents) {
                uint8_t rpt_buf[max_touch_rpt_sz];
                ssize_t bytes = read(touchfd, rpt_buf, max_touch_rpt_sz);
                ZX_ASSERT(bytes > 0);

                uint8_t id = *(uint8_t*)rpt_buf;
                if (id == PARADISE_RPT_ID_TOUCH) {
                    paradise_touch_t* rpt = (paradise_touch_t*)rpt_buf;

                    for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                        if (paradise_finger_flags_tswitch(rpt->fingers[c].flags)) {
                            sprites[c].x = scale(rpt->fingers[c].x, width, PARADISE_X_MAX);
                            sprites[c].y = scale(rpt->fingers[c].y, height, PARADISE_Y_MAX);
                        } else {
                            sprites[c].x = INT_MAX;
                            sprites[c].y = INT_MAX;
                        }
                    }
                } else if (id == PARADISE_RPT_ID_STYLUS) {
                    paradise_stylus_t* rpt = (paradise_stylus_t*)rpt_buf;

                    point_t new_pen = {.x = INT_MAX, .y = INT_MAX};
                    if (paradise_stylus_status_tswitch(rpt->status)) {
                        new_pen.x = scale(rpt->x, width, PARADISE_STYLUS_X_MAX);
                        new_pen.y = scale(rpt->y, height, PARADISE_STYLUS_Y_MAX);
                    }

                    if (new_pen.x != INT_MAX && new_pen.y != INT_MAX) {
                        if (new_pen.x != pen.x || new_pen.y != pen.y) {
                            line_t line = {.p0 = pen, .p1 = new_pen};

                            for (auto& buffer : buffers) {
                                buffer.pending_lines.push_back(line);
                            }
                        }
                    }
                    pen = new_pen;
                }
            }
            if (touchpadfd >= 0 && fds[1].revents) {
                uint8_t rpt_buf[max_touchpad_rpt_sz];
                ssize_t bytes = read(touchpadfd, rpt_buf, max_touchpad_rpt_sz);
                ZX_ASSERT(bytes > 0);

                paradise_touchpad_t* rpt = (paradise_touchpad_t*)rpt_buf;
                for (uint8_t c = 0; c < NUM_SPRITES; c++) {
                    if (rpt->fingers[c].tip_switch) {
                        sprites[c].x = scale(rpt->fingers[c].x, width, PARADISE_X_MAX);
                        sprites[c].y = scale(rpt->fingers[c].y, height, PARADISE_Y_MAX);
                    } else {
                        sprites[c].x = INT_MAX;
                        sprites[c].y = INT_MAX;
                    }
                }
            }
        } while (ready);

        size_t idx = 0;
        switch (vsync) {
        case VSync::ON:
        case VSync::ADAPTIVE:
            idx = update % buffers.size();
            break;
        case VSync::OFF:
            break;
        }

        uint8_t* pixels = (uint8_t*)buffers[idx].data;
        gfx_surface* surface = buffers[idx].surface;

        // Update path for current image.
        if (!buffers[idx].pending_lines.is_empty()) {
            TRACE_DURATION("app", "UpdatePath");

            int64_t xmin = width;
            int64_t xmax = 0;
            int64_t ymin = height;
            int64_t ymax = 0;

            for (auto& line : buffers[idx].pending_lines) {
                xmin = MIN(MIN(line.p0.x, line.p1.x), xmin);
                xmax = MAX(MAX(line.p0.x, line.p1.x), xmax);
                ymin = MIN(MIN(line.p0.y, line.p1.y), ymin);
                ymax = MAX(MAX(line.p0.y, line.p1.y), ymax);

                gfx_line(surface, line.p0.x, line.p0.y, line.p1.x, line.p1.y, /*color=*/0);
            }

            xmin = CLAMP(xmin, 0, width);
            xmax = CLAMP(xmax + 1, 0, width);
            ymin = CLAMP(ymin, 0, height);
            ymax = CLAMP(ymax + 1, 0, height);

            if (xmin < xmax && ymin < ymax) {
                for (int64_t y = ymin; y < ymax; ++y) {
                    uint8_t* p = pixels + y * surface->stride * surface->pixelsize;

                    zx_cache_flush(p + xmin * surface->pixelsize,
                                   (xmax - xmin) * surface->pixelsize,
                                   ZX_CACHE_FLUSH_DATA);
                }
            }
            buffers[idx].pending_lines.reset();
        }

        // Update sprites for current image.
        if (memcmp(sprites, buffers[idx].sprites, sizeof(sprites))) {
            TRACE_DURATION("app", "UpdateSprites");

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

                    y1 = sprites[c].y - SPRITE_DIMEN;
                    y2 = sprites[c].y + SPRITE_DIMEN;
                    if (y1 < y && y2 > y) {
                        xmin = MIN(xmin, sprites[c].x - SPRITE_DIMEN);
                        xmax = MAX(xmax, sprites[c].x + SPRITE_DIMEN);
                    }
                    y1 = buffers[idx].sprites[c].y - SPRITE_DIMEN;
                    y2 = buffers[idx].sprites[c].y + SPRITE_DIMEN;
                    if (y1 < y && y2 > y) {
                        xmin = MIN(xmin, buffers[idx].sprites[c].x - SPRITE_DIMEN);
                        xmax = MAX(xmax, buffers[idx].sprites[c].x + SPRITE_DIMEN);
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
                            int64_t x1 = sprites[c].x - SPRITE_DIMEN;
                            int64_t x2 = sprites[c].x + SPRITE_DIMEN;
                            int64_t y1 = sprites[c].y - SPRITE_DIMEN;
                            int64_t y2 = sprites[c].y + SPRITE_DIMEN;

                            if (x1 < x && x2 > x && y1 < y && y2 > y) {
                                static uint32_t colors[] = {
                                    0x00ff0000,
                                    0x0000ff00,
                                    0x000000ff,
                                    0x00ffff00,
                                    0x00ff00ff,
                                };
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

            memcpy(buffers[idx].sprites, sprites, sizeof(sprites));
        }

        // Signal wait event if not using adaptive sync.
        if (buffers[idx].wait_event_id != FB_INVALID_ID)
            zx_object_signal(buffers[idx].wait_event, 0, ZX_EVENT_SIGNALED);

        ++update;
    });

    uint32_t frame = 0;
    async::TaskClosure frame_task([&loop,
                                   &update_loop,
                                   &frame_task,
                                   &update_task,
                                   &vsync,
                                   &vsync_offset,
                                   &touchfd,
                                   &touchpadfd,
                                   &buffers,
                                   &frame] {
        switch (vsync) {
        case VSync::ON:
        case VSync::ADAPTIVE: {
            zx_time_t vsync_time;
            zx_status_t status;

            // Frame number determine buffer index of current frame.
            size_t idx = frame % buffers.size();

            // Wait for VSync.
            while (true) {
                TRACE_DURATION("app", "WaitForVsync");

                uint64_t image_id = FB_INVALID_ID;
                while ((status = fb_wait_for_vsync(&vsync_time, &image_id)) != ZX_OK) {
                    if (status == ZX_ERR_STOP) {
                        loop.Quit();
                        return;
                    }
                }

                // Stop when image from last frame is being scanned out.
                if (image_id == buffers[idx].image_id)
                    break;

                fprintf(stderr, "warning: missed frame!\n");
            }

            // Advance frame number update buffer index.
            idx = ++frame % buffers.size();

            // Wait until vsync + offset.
            {
                TRACE_DURATION("app", "WaitForVsyncOffset");

                zx_nanosleep(vsync_time + vsync_offset);
            }

            // Reset wait event.
            if (buffers[idx].wait_event_id != FB_INVALID_ID)
                zx_object_signal(buffers[idx].wait_event, ZX_EVENT_SIGNALED, 0);

            // Present buffer. wait_event_id is invalid when using adaptive sync as
            // that allows scanout to start event if we haven't finished producing
            // the new frame.
            if ((status = fb_present_image2(buffers[idx].image_id,
                                            buffers[idx].wait_event_id,
                                            FB_INVALID_ID)) != ZX_OK) {
                fprintf(stderr, "failed to present framebuffer\n");
                loop.Quit();
                return;
            }

            // Schedule update on update thread.
            update_task.Post(update_loop.dispatcher());
        } break;
        case VSync::OFF: {
            int startfd, endfd;
            struct pollfd fds[2];

            // Wait for input.
            prepare_poll(touchfd, touchpadfd, &startfd, &endfd, fds);
            poll(&fds[startfd], endfd - startfd, -1 /* infinite timeout*/);

            // Update buffer on main thread.
            update_task.Post(loop.dispatcher());
        } break;
        }

        frame_task.Post(loop.dispatcher());
    });
    frame_task.Post(loop.dispatcher());

    loop.Run();

    if (touchfd >= 0)
        close(touchfd);
    if (touchpadfd >= 0)
        close(touchpadfd);
    for (auto& buffer : buffers) {
        fb_release_image(buffer.image_id);
        if (buffer.wait_event_id != FB_INVALID_ID)
            fb_release_event(buffer.wait_event_id);
        if (buffer.wait_event != ZX_HANDLE_INVALID)
            zx_handle_close(buffer.wait_event);
        gfx_surface_destroy(buffer.surface);
        zx_vmar_unmap(zx_vmar_root_self(), buffer.data, buffer_size);
        zx_handle_close(buffer.vmo);
    }
    fb_release();
    return 0;
}
