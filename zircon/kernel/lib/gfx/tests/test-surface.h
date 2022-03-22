// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_GFX_TESTS_TEST_SURFACE_H_
#define ZIRCON_KERNEL_LIB_GFX_TESTS_TEST_SURFACE_H_

#include <lib/gfx/surface.h>

// surface setup
gfx::Surface* CreateTestSurface(void* ptr, unsigned width, unsigned height, unsigned stride,
                                unsigned format, uint32_t flags);

#endif  // ZIRCON_KERNEL_LIB_GFX_TESTS_TEST_SURFACE_H_
