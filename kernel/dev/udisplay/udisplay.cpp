// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <dev/udisplay.h>
#include <lib/crashlog.h>
#include <lib/debuglog.h>
#include <lib/gfxconsole.h>
#include <lib/io.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#define LOCAL_TRACE 0

constexpr uint kFramebufferArchMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

static char crashlogbuf[4096u];

struct udisplay_info {
    void* framebuffer_virt;
    size_t framebuffer_size;
    struct display_info info;
    fbl::RefPtr<VmMapping> framebuffer_vmo_mapping;
};

static struct udisplay_info g_udisplay = {};

zx_status_t udisplay_init(void) {
    return ZX_OK;
}

void dlog_bluescreen_halt(void) {
    size_t len = crashlog_to_string(crashlogbuf, sizeof(crashlogbuf));
    platform_stow_crashlog(crashlogbuf, len);
    if (g_udisplay.framebuffer_virt == 0)
        return;
}

void udisplay_clear_framebuffer_vmo() {
    if (g_udisplay.framebuffer_vmo_mapping) {
        g_udisplay.framebuffer_size = 0;
        g_udisplay.framebuffer_virt = 0;
        g_udisplay.framebuffer_vmo_mapping->Destroy();
        g_udisplay.framebuffer_vmo_mapping = nullptr;
    }
}

zx_status_t udisplay_set_framebuffer(fbl::RefPtr<VmObject> vmo) {
    udisplay_clear_framebuffer_vmo();

    const size_t size = vmo->size();
    fbl::RefPtr<VmMapping> mapping;
    zx_status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
        0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */,
        fbl::move(vmo), 0, kFramebufferArchMmuFlags, "framebuffer_vmo", &mapping);
    if (status != ZX_OK)
        return status;

    status = mapping->MapRange(0, size, true);
    if (status != ZX_OK) {
        mapping->Destroy();
        return status;
    }

    g_udisplay.framebuffer_virt = reinterpret_cast<void*>(mapping->base());
    g_udisplay.framebuffer_size = size;
    g_udisplay.framebuffer_vmo_mapping = mapping;
    return ZX_OK;
}

zx_status_t udisplay_set_display_info(struct display_info* display) {
    memcpy(&g_udisplay.info, display, sizeof(struct display_info));
    return ZX_OK;
}

zx_status_t udisplay_bind_gfxconsole(void) {
    if (g_udisplay.framebuffer_virt == 0)
        return ZX_ERR_NOT_FOUND;

    // bind the display to the gfxconsole
    g_udisplay.info.framebuffer = g_udisplay.framebuffer_virt;
    g_udisplay.info.flags = DISPLAY_FLAG_NEEDS_CACHE_FLUSH | DISPLAY_FLAG_CRASH_FRAMEBUFFER;
    gfxconsole_bind_display(&g_udisplay.info, nullptr);

    return ZX_OK;
}
