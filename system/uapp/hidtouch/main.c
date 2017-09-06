// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hid/acer12.h>
#include <hid/paradise.h>
#include <hid/usages.h>

#include <magenta/device/console.h>
#include <magenta/device/display.h>
#include <magenta/device/input.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#define DEV_INPUT       "/dev/class/input"
#define FRAMEBUFFER     "/dev/class/framebuffer/000"
#define CLEAR_BTN_SIZE 50
#define I2C_HID_DEBUG 0

enum touch_panel_type {
    TOUCH_PANEL_UNKNOWN,
    TOUCH_PANEL_ACER12,
    TOUCH_PANEL_PARADISE,
};

// Array of colors for each finger
static uint32_t colors[] = {
    0x00ff0000,
    0x0000ff00,
    0x000000ff,
    0x00ffff00,
    0x00ff00ff,
    0x0000ffff,
    0x00000000,
    0x00f0f0f0,
    0x00f00f00,
    0x000ff000,
};

static void acer12_touch_dump(acer12_touch_t* rpt) {
    printf("report id: %u\n", rpt->rpt_id);
    for (int i = 0; i < 5; i++) {
        printf("finger %d\n", i);
        printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
        printf("    tswitch: %u\n", acer12_finger_id_tswitch(rpt->fingers[i].finger_id));
        printf("    contact: %u\n", acer12_finger_id_contact(rpt->fingers[i].finger_id));
        printf("  width:  %u\n", rpt->fingers[i].width);
        printf("  height: %u\n", rpt->fingers[i].height);
        printf("  x:      %u\n", rpt->fingers[i].x);
        printf("  y:      %u\n", rpt->fingers[i].y);
    }
    printf("scan_time: %u\n", rpt->scan_time);
    printf("contact count: %u\n", rpt->contact_count);
}

static void paradise_touch_dump(paradise_touch_t* rpt) {
    printf("report id: %u\n", rpt->rpt_id);
    printf("pad: %#02x\n", rpt->pad);
    printf("contact count: %u\n", rpt->contact_count);
    for (int i = 0; i < 5; i++) {
        printf("finger %d\n", i);
        printf("  flags: %#02x\n", rpt->fingers[i].flags);
        printf("    tswitch: %u\n", paradise_finger_flags_tswitch(rpt->fingers[i].flags));
        printf("    confidence: %u\n", paradise_finger_flags_confidence(rpt->fingers[i].flags));
        printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
        printf("  x:      %u\n", rpt->fingers[i].x);
        printf("  y:      %u\n", rpt->fingers[i].y);
    }
    printf("scan_time: %u\n", rpt->scan_time);
}

static uint32_t scale32(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim) {
    return (z * screen_dim) / rpt_dim;
}

static void draw_points(uint32_t* pixels, uint32_t color, uint32_t x, uint32_t y, uint8_t width, uint8_t height, uint32_t fbwidth, uint32_t fbheight) {
    uint32_t xrad = (width + 1) / 2;
    uint32_t yrad = (height + 1) / 2;

    uint32_t xmin = (xrad > x) ? 0 : x - xrad;
    uint32_t xmax = (xrad > fbwidth - x) ? fbwidth : x + xrad;
    uint32_t ymin = (yrad > y) ? 0 : y - yrad;
    uint32_t ymax = (yrad > fbheight - y) ? fbheight : y + yrad;

    for (uint32_t px = xmin; px < xmax; px++) {
        for (uint32_t py = ymin; py < ymax; py++) {
            *(pixels + py * fbwidth + px) = color;
        }
    }
}

static uint32_t get_color(uint8_t c) {
    return colors[c];
}

static void clear_screen(void* buf, ioctl_display_get_fb_t* fb) {
    memset(buf, 0xff, fb->info.pixelsize * fb->info.stride * fb->info.height);
    draw_points((uint32_t*)buf, 0xff00ff, fb->info.stride - (CLEAR_BTN_SIZE / 2), (CLEAR_BTN_SIZE / 2),
            CLEAR_BTN_SIZE, CLEAR_BTN_SIZE, fb->info.stride, fb->info.height);
}

static void process_acer12_touchscreen_input(void* buf, size_t len, int vcfd, uint32_t* pixels,
        ioctl_display_get_fb_t* fb) {
    acer12_touch_t* rpt = buf;
    if (len < sizeof(*rpt)) {
        printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
        return;
    }
#if I2C_HID_DEBUG
    acer12_touch_dump(rpt);
#endif
    for (uint8_t c = 0; c < 5; c++) {
        if (!acer12_finger_id_tswitch(rpt->fingers[c].finger_id % 10)) continue;
        uint32_t x = scale32(rpt->fingers[c].x, fb->info.width, ACER12_X_MAX);
        uint32_t y = scale32(rpt->fingers[c].y, fb->info.height, ACER12_Y_MAX);
        uint32_t width = 2 * rpt->fingers[c].width;
        uint32_t height = 2 * rpt->fingers[c].height;
        uint32_t color = get_color(acer12_finger_id_contact(rpt->fingers[c].finger_id));
        draw_points(pixels, color, x, y, width, height, fb->info.stride, fb->info.height);
    }

    if (acer12_finger_id_tswitch(rpt->fingers[0].finger_id)) {
        uint32_t x = scale32(rpt->fingers[0].x, fb->info.width, ACER12_X_MAX);
        uint32_t y = scale32(rpt->fingers[0].y, fb->info.height, ACER12_Y_MAX);
        if (x + CLEAR_BTN_SIZE > fb->info.width && y < CLEAR_BTN_SIZE) {
            clear_screen(pixels, fb);
        }
    }
    ssize_t ret = ioctl_display_flush_fb(vcfd);
    if (ret < 0) {
        printf("failed to flush: %zd\n", ret);
    }
}

static void process_paradise_touchscreen_input(void* buf, size_t len, int vcfd, uint32_t* pixels,
        ioctl_display_get_fb_t* fb) {
    paradise_touch_t* rpt = buf;
    if (len < sizeof(*rpt)) {
        printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
        return;
    }
#if I2C_HID_DEBUG
    paradise_touch_dump(rpt);
#endif
    for (uint8_t c = 0; c < 5; c++) {
        if (!paradise_finger_flags_tswitch(rpt->fingers[c].flags)) continue;
        uint32_t x = scale32(rpt->fingers[c].x, fb->info.width, PARADISE_X_MAX);
        uint32_t y = scale32(rpt->fingers[c].y, fb->info.height, PARADISE_Y_MAX);
        uint32_t width = 10;
        uint32_t height = 10;
        uint32_t color = get_color(c);
        draw_points(pixels, color, x, y, width, height, fb->info.stride, fb->info.height);
    }

    if (paradise_finger_flags_tswitch(rpt->fingers[0].flags)) {
        uint32_t x = scale32(rpt->fingers[0].x, fb->info.width, PARADISE_X_MAX);
        uint32_t y = scale32(rpt->fingers[0].y, fb->info.height, PARADISE_Y_MAX);
        if (x + CLEAR_BTN_SIZE > fb->info.width && y < CLEAR_BTN_SIZE) {
            clear_screen(pixels, fb);
        }
    }
    ssize_t ret = ioctl_display_flush_fb(vcfd);
    if (ret < 0) {
        printf("failed to flush: %zd\n", ret);
    }
}

static void process_acer12_stylus_input(void* buf, size_t len, int vcfd, uint32_t* pixels,
        ioctl_display_get_fb_t* fb) {
    acer12_stylus_t* rpt = buf;
    if (len < sizeof(*rpt)) {
        printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
        return;
    }
    // Don't draw for out of range or hover with no switches.
    if (!rpt->status || rpt->status == ACER12_STYLUS_STATUS_INRANGE) return;

    uint32_t x = scale32(rpt->x, fb->info.width, ACER12_STYLUS_X_MAX);
    uint32_t y = scale32(rpt->y, fb->info.height, ACER12_STYLUS_Y_MAX);
    // Pressing the clear button requires contact (not just hover).
    if (acer12_stylus_status_tswitch(rpt->status)) {
        if (x + CLEAR_BTN_SIZE > fb->info.width && y < CLEAR_BTN_SIZE) {
            clear_screen(pixels, fb);
            goto flush;
        }
    }
    uint32_t size, color;
    size = acer12_stylus_status_tswitch(rpt->status) ? rpt->pressure >> 4 : 4;
    switch (rpt->status) {
    case 3: // in_range | tip_switch
        color = get_color(0);
        break;
    case 5: // in_range | barrel_switch
        color = get_color(1);
        break;
    case 7: // in_range | tip_switch | barrel_switch
        color = get_color(4);
        break;
    case 9: // in_range | invert
        color = get_color(5);
        break;
    case 17: // in_range | erase (== tip_switch | invert)
        color = 0x00ffffff;
        size = 32;  // fixed size eraser
        break;
    default:
        printf("unknown rpt->status=%u\n", rpt->status);
        color = get_color(6);
        break;
    }

    draw_points(pixels, color, x, y, size, size, fb->info.stride, fb->info.height);

flush: ;
    ssize_t ret = ioctl_display_flush_fb(vcfd);
    if (ret < 0) {
        printf("failed to flush: %zd\n", ret);
    }
}

int main(int argc, char* argv[]) {
    int vcfd = open(FRAMEBUFFER, O_RDWR);
    if (vcfd < 0) {
        printf("failed to open %s: %d\n", FRAMEBUFFER, errno);
        return -1;
    }

    ioctl_display_get_fb_t fb;
    ssize_t ret = ioctl_display_get_fb(vcfd, &fb);
    if (ret < 0) {
        printf("failed to get FB: %zd\n", ret);
        return -1;
    }
    if (fb.info.pixelsize != 4) {
        printf("only 32-bit framebuffers are supported for now!\n");
        return -1;
    }

    printf("format = %d\n", fb.info.format);
    printf("width = %d\n", fb.info.width);
    printf("height = %d\n", fb.info.height);
    printf("stride = %d\n", fb.info.stride);
    printf("pixelsize = %d\n", fb.info.pixelsize);
    printf("flags = 0x%x\n", fb.info.flags);


    size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
    uintptr_t fbo;

    mx_status_t status = _mx_vmar_map(mx_vmar_root_self(), 0, fb.vmo, 0, size,
                                      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &fbo);
    if (status < 0) {
        printf("couldn't map fb: %d\n", status);
        return -1;
    }
    uint32_t* pixels32 = (uint32_t*)fbo;

    // Scan /dev/class/input to find the touchscreen
    struct dirent* de;
    DIR* dir = opendir(DEV_INPUT);
    if (!dir) {
        printf("failed to open %s: %d\n", DEV_INPUT, errno);
        return -1;
    }

    int touchfd = -1;
    size_t rpt_desc_len = 0;
    uint8_t* rpt_desc = NULL;
    enum touch_panel_type panel = TOUCH_PANEL_UNKNOWN;
    while ((de = readdir(dir)) != NULL) {
        char devname[128];

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }

        snprintf(devname, sizeof(devname), "%s/%s", DEV_INPUT, de->d_name);
        touchfd = open(devname, O_RDONLY);
        if (touchfd < 0) {
            printf("failed to open %s: %d\n", devname, errno);
            continue;
        }

        ret = ioctl_input_get_report_desc_size(touchfd, &rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor length for %s: %zd\n", devname, ret);
            goto next_node;
        }

        rpt_desc = malloc(rpt_desc_len);
        if (rpt_desc == NULL) {
            printf("no memory!\n");
            exit(-1);
        }

        ret = ioctl_input_get_report_desc(touchfd, rpt_desc, rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor for %s: %zd\n", devname, ret);
            goto next_node;
        }

        if (is_acer12_touch_report_desc(rpt_desc, rpt_desc_len)) {
            panel = TOUCH_PANEL_ACER12;
            // Found the touchscreen
            printf("touchscreen: %s\n", devname);
            break;
        }

        if (is_paradise_touch_report_desc(rpt_desc, rpt_desc_len)) {
            panel = TOUCH_PANEL_PARADISE;
            // Found the touchscreen
            printf("touchscreen: %s\n", devname);
            break;
        }

next_node:
        rpt_desc_len = 0;

        if (rpt_desc != NULL) {
            free(rpt_desc);
            rpt_desc = NULL;
        }

        if (touchfd >= 0) {
            close(touchfd);
            touchfd = -1;
        }
    }
    closedir(dir);

    if (touchfd < 0) {
        printf("could not find a touchscreen!\n");
        return -1;
    }
    assert(rpt_desc_len > 0);
    assert(rpt_desc);

    input_report_size_t max_rpt_sz = 0;
    ret = ioctl_input_get_max_reportsize(touchfd, &max_rpt_sz);
    if (ret < 0) {
        printf("failed to get max report size: %zd\n", ret);
        return -1;
    }
    void* buf = malloc(max_rpt_sz);
    if (buf == NULL) {
        printf("no memory!\n");
        return -1;
    }

    ret = ioctl_console_set_active_vc(vcfd);
    if (ret < 0) {
        printf("could not set active console: %zd\n", ret);
        // not a fatal error
        printf("press Alt-Tab to switch consoles\n");
    }

    clear_screen((void*)fbo, &fb);
    while (1) {
        ssize_t r = read(touchfd, buf, max_rpt_sz);
        if (r < 0) {
            printf("touchscreen read error: %zd (errno=%d)\n", r, errno);
            break;
        }
        if (panel == TOUCH_PANEL_ACER12) {
            if (*(uint8_t*)buf == ACER12_RPT_ID_TOUCH) {
                process_acer12_touchscreen_input(buf, r, vcfd, pixels32, &fb);
            } else if (*(uint8_t*)buf == ACER12_RPT_ID_STYLUS) {
                process_acer12_stylus_input(buf, r, vcfd, pixels32, &fb);
            }
        } else if (panel == TOUCH_PANEL_PARADISE) {
            if (*(uint8_t*)buf == PARADISE_RPT_ID_TOUCH) {
                process_paradise_touchscreen_input(buf, r, vcfd, pixels32, &fb);
            }
        }
    }

    free(buf);
    free(rpt_desc);
    close(touchfd);
    _mx_vmar_unmap(mx_vmar_root_self(), fbo, size);
    close(vcfd);
    return 0;
}
