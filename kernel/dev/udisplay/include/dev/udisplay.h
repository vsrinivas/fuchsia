// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/display.h>
#include <err.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t udisplay_init(void);
zx_status_t udisplay_set_framebuffer(paddr_t fb_phys, size_t fb_size);
zx_status_t udisplay_set_display_info(struct display_info* display);
zx_status_t udisplay_bind_gfxconsole(void);

__END_CDECLS

#ifdef __cplusplus

#include <fbl/ref_ptr.h>

class VmObject;
zx_status_t udisplay_set_framebuffer_vmo(fbl::RefPtr<VmObject> vmo);
void udisplay_clear_framebuffer_vmo(void);

#endif
