// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/device/display.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

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
    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, fb.vmo, 0, size,
                                     MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &fbo);
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

    double a,b, dx, dy, mag, c, ci;
    uint32_t color,iter,x,y;

    bool rotate = (gfx->height > gfx->width);

    dx= 3.0/((double)gfx->width);
    dy= 3.0/((double)gfx->height);
    c = -2.0;
    ci = -1.5;
    for (y = 0; y < gfx->height; y++) {
        if (rotate) {
            ci = -1.5;
        } else {
            c = -2.0;
        }
        for (x = 0; x < gfx->width; x++) {
            a=0;
            b=0;
            mag=0;
            iter = 0;
            while ((mag < 4.0) && (iter < 200) ){
                double a1;
                a1 = a*a - b*b + c;
                b = 2.0 * a * b + ci;
                a=a1;
                mag = a*a + b*b;
                iter++;
            }
            if (rotate) {
                ci = ci + dx;
            } else {
                c = c + dx;
            }
            if (iter == 200) {
                color = 0;
            } else {
                color = 0x231AF9 * iter;
            }
            color= color | 0xff000000;
            gfx_putpixel(gfx, x, y, color);

        }
        if ((y%50) == 0)
            ioctl_display_flush_fb(vfd);
        if (rotate) {
            c = c + dy;
        } else {
            ci = ci + dy;
        }
    }
    ioctl_display_flush_fb(vfd);
    mx_nanosleep(mx_deadline_after(MX_SEC(10)));

    gfx_surface_destroy(gfx);
    close(vfd);
}
