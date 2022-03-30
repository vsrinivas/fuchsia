// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_GFX_H_
#define ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_GFX_H_

#include <sys/types.h>

#include <dev/display.h>

#include "surface.h"

// TODO(fxbug.dev/96043): Delete this header. These functions should be Surface
// methods or internal.

namespace gfx {

// utility routine to make a surface out of a display info
Surface* CreateSurfaceFromDisplay(display_info*);
zx_status_t InitSurfaceFromDisplay(Surface* surface, display_info*);

// utility routine to fill the display with a little moire pattern
void DrawPattern(void);

}  // namespace gfx

#endif  // ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_GFX_H_
