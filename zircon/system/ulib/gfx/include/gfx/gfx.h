// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GFX_GFX_H_
#define GFX_GFX_H_

#include <gfx-common/gfx-common.h>

__BEGIN_CDECLS

// surface setup
gfx_surface* gfx_create_surface(void* ptr, unsigned width, unsigned height, unsigned stride,
                                unsigned format, uint32_t flags);
__END_CDECLS

#endif  // GFX_GFX_H_
