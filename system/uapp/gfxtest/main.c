// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <zircon/device/display.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <gfx/gfx.h>

int main(int argc, char* argv[]) {
    int vfd = open("/dev/class/framebuffer/000", O_RDWR);
    if (vfd < 0) {
        printf("failed to open fb (%d)\n", errno);
        return -1;
    }
    ioctl_display_get_fb_t fb;
    if (ioctl_display_get_fb(vfd, &fb) != sizeof(fb)) {
        printf("failed to get fb\n");
        return -1;
    }

    size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
    uintptr_t fbo;
    zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, fb.vmo, 0, size,
                                     ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &fbo);
    if (status < 0) {
        printf("failed to map fb (%d)\n", status);
        return -1;
    }

    gfx_surface* gfx = gfx_create_surface((void*)fbo, fb.info.width, fb.info.height, fb.info.stride,
                                          fb.info.format, 0);
    if (!gfx) {
        printf("failed to create gfx surface\n");
        return -1;
    }
    gfx_fillrect(gfx, 0, 0, gfx->width, gfx->height, 0xffffffff);

    int d = gfx->height / 5;
    int i = 10;
    while (i--) {
        zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
        gfx_fillrect(gfx, (gfx->width - d) / 2, (gfx->height - d) / 2, d, d, i % 2 ? 0xff55ff55 : 0xffaa00aa);
        ioctl_display_flush_fb(vfd);
    }

    gfx_surface_destroy(gfx);
    close(vfd);
}
