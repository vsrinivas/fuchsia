// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/udisplay.h>
#include <lib/debuglog.h>
#include <lib/gfxconsole.h>
#include <lib/io.h>
#include <kernel/vm.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <qrcodegen/qrcode.h>

#define LOCAL_TRACE 0

static qrcodegen::QrCode qrcode;

#define MAX_QRCODE_DATA (2953u)
static char qrlogbuf[MAX_QRCODE_DATA];
static size_t qrlogptr;

static void qrcode_print_callback(print_callback_t* cb, const char* str, size_t len) {
    if (len > (MAX_QRCODE_DATA - qrlogptr))
        len = (MAX_QRCODE_DATA - qrlogptr);

    memcpy(qrlogbuf + qrlogptr, str, len);
    qrlogptr += len;
}

static print_callback_t qrcode_cb = {
    .entry = {},
    .print = qrcode_print_callback,
    .context = NULL
};

struct udisplay_info {
    paddr_t framebuffer_phys;
    void* framebuffer_virt;
    size_t framebuffer_size;
    struct display_info info;
};

static struct udisplay_info g_udisplay;

status_t udisplay_init(void) {
    return NO_ERROR;
}

void dlog_bluescreen_halt(void) {
    if (g_udisplay.framebuffer_virt == 0)
        return;

    if (qrcode.encodeBinary(qrlogbuf, qrlogptr, qrcodegen::Ecc::LOW)) {
        printf("cannot create qrcode\n");
        return;
    }

    int w = g_udisplay.info.width;
    int h = g_udisplay.info.height;

    // qrcode.pixel() returns infinite white pixels if you
    // access outside of the body of the qrcode, which we
    // take advantage of to draw a border around the qrcode
    // (necessary for good recogniation) by overshooting 3
    // "pixels" in every direction.
    int sz = qrcode.size() + 6;
    int px = 1;

    // Scale up a bit if there's room, but don't go crazy
    // (no more than 5x5 pixel scaling)
    while (((sz * (px + 1)) < (w / 2)) && (px < 5))
        px++;

    w -= sz * px;
    h -= sz * px;

    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            uint32_t color = qrcode.pixel(x - 3, y - 3) ? 0xFF000000 : 0xFFFFFFFF;
            for (int yy = 0; yy < px; yy++) {
                for (int xx = 0; xx < px; xx++) {
                    gfxconsole_putpixel(w + x * px + xx, h + y * px + yy, color);
                }
            }
        }
    }

}

status_t udisplay_set_framebuffer(paddr_t fb_phys, void* fb_user_virt, size_t fb_size) {
    g_udisplay.framebuffer_phys = fb_phys;
    g_udisplay.framebuffer_size = fb_size;

    // map the framebuffer
    vmm_aspace_t* aspace = vmm_get_kernel_aspace();
    status_t result = vmm_alloc_physical(
            aspace,
            "udisplay_fb",
            g_udisplay.framebuffer_size,
            &g_udisplay.framebuffer_virt,
            PAGE_SIZE_SHIFT,
            0 /* min alloc gap */,
            g_udisplay.framebuffer_phys,
            0 /* vmm flags */,
            ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);

    if (result)
        g_udisplay.framebuffer_virt = 0;

    return NO_ERROR;
}

status_t udisplay_set_display_info(struct display_info* display) {
    memcpy(&g_udisplay.info, display, sizeof(struct display_info));
    return NO_ERROR;
}

status_t udisplay_bind_gfxconsole(void) {
    if (g_udisplay.framebuffer_virt == 0)
        return ERR_NOT_FOUND;

    register_print_callback(&qrcode_cb);

    // bind the display to the gfxconsole
    g_udisplay.info.framebuffer = g_udisplay.framebuffer_virt;
    g_udisplay.info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER | DISPLAY_FLAG_CRASH_FRAMEBUFFER;
    gfxconsole_bind_display(&g_udisplay.info, NULL);

    return NO_ERROR;
}
