// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/display.h>
#include <magenta/compiler.h>
#include <err.h>

__BEGIN_CDECLS

status_t udisplay_init(void);
status_t udisplay_set_framebuffer(paddr_t fb_phys, size_t fb_size);
status_t udisplay_set_display_info(struct display_info* display);
status_t udisplay_bind_gfxconsole(void);

__END_CDECLS

#ifdef __cplusplus
#include <object/dispatcher.h>

class VmObject;
status_t udisplay_set_framebuffer_vmo(fbl::RefPtr<VmObject> vmo);
#endif
