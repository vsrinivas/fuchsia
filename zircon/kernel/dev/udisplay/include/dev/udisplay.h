// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_UDISPLAY_INCLUDE_DEV_UDISPLAY_H_
#define ZIRCON_KERNEL_DEV_UDISPLAY_INCLUDE_DEV_UDISPLAY_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <dev/display.h>

__BEGIN_CDECLS

zx_status_t udisplay_init(void);
zx_status_t udisplay_set_display_info(struct display_info* display);
zx_status_t udisplay_bind_gfxconsole(void);

__END_CDECLS

#ifdef __cplusplus

#include <fbl/ref_ptr.h>

class VmObject;
zx_status_t udisplay_set_framebuffer(fbl::RefPtr<VmObject> vmo);
void udisplay_clear_framebuffer_vmo(void);

#endif

#endif  // ZIRCON_KERNEL_DEV_UDISPLAY_INCLUDE_DEV_UDISPLAY_H_
