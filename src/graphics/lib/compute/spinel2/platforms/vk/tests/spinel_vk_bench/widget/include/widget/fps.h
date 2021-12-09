// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_FPS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_FPS_H_

//
//
//

#include <stdint.h>

#include "widget/widget.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

WIDGET_TYPEDEF(widget_fps);

//
//
//

widget_fps_t
widget_fps_create(float glyph_width);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_FPS_H_
