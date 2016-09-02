// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/udisplay.h>
#include <lib/gfxconsole.h>
#include <kernel/vm.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

struct udisplay_info {
    paddr_t framebuffer_phys;
    void* framebuffer_user_virt;
    void* framebuffer_virt;
    size_t framebuffer_size;
    struct display_info info;
};

static struct udisplay_info g_udisplay;

status_t udisplay_init(void) {
    return NO_ERROR;
}

status_t udisplay_set_framebuffer(paddr_t fb_phys, void* fb_user_virt, size_t fb_size) {
    g_udisplay.framebuffer_phys = fb_phys;
    g_udisplay.framebuffer_user_virt = fb_user_virt;
    g_udisplay.framebuffer_size = fb_size;

    return NO_ERROR;
}

status_t udisplay_set_display_info(struct display_info* display) {
    memcpy(&g_udisplay.info, display, sizeof(struct display_info));
    return NO_ERROR;
}

status_t udisplay_bind_gfxconsole(void) {
    assert(g_udisplay.framebuffer_phys);
    assert(g_udisplay.framebuffer_size);

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
    if (result) return result;

    // bind the display to the gfxconsole
    g_udisplay.info.framebuffer = g_udisplay.framebuffer_virt;
    g_udisplay.info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER | DISPLAY_FLAG_CRASH_FRAMEBUFFER;
    gfxconsole_bind_display(&g_udisplay.info, NULL);

    return NO_ERROR;
}
