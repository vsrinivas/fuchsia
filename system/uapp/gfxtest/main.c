#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <magenta/types.h>
#include <magenta/device/console.h>
#include <magenta/device/display.h>
#include <magenta/syscalls.h>

#include <gfx/gfx.h>

int main(int argc, char* argv[]) {
    uint32_t fs = 0;
    if (argc > 1 && !strcmp(argv[1], "-f")) {
        fs = 1;
    }

    int vfd = open("/dev/class/console/vc", O_RDWR);
    if (vfd < 0) {
        printf("failed to open virtcon (%d)\n", errno);
        return -1;
    }
    ioctl_display_get_fb_t fb;
    if (ioctl_display_get_fb(vfd, &fb) != sizeof(fb)) {
        printf("failed to get fb\n");
        return -1;
    }

    size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
    uintptr_t fbo;
    mx_status_t status = mx_process_map_vm(mx_process_self(), fb.vmo, 0, size, &fbo, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status < 0) {
        printf("failed to map fb (%d)\n", status);
        return -1;
    }

    gfx_surface* gfx = gfx_create_surface((void*)fbo, fb.info.width, fb.info.height, fb.info.stride, fb.info.format, 0);
    if (!gfx) {
        printf("failed to create gfx surface\n");
        return -1;
    }
    gfx_fillrect(gfx, 0, 0, gfx->width, gfx->height, 0xffffffff);

    ioctl_display_set_fullscreen(vfd, &fs);
    ioctl_console_set_active_vc(vfd);

    int d = gfx->height / 5;
    int i = 10;
    while (i--) {
        mx_nanosleep(MX_SEC(1));
        gfx_fillrect(gfx, (gfx->width - d) / 2, (gfx->height - d) / 2, d, d, i % 2 ? 0xff55ff55 : 0xffaa00aa);
        ioctl_display_flush_fb(vfd);
    }

    gfx_surface_destroy(gfx);
    close(vfd);
}
