// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_SVG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_SVG_H_

//
//
//

#include <stdint.h>

#include "svg/svg.h"
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

WIDGET_TYPEDEF(widget_svg);

//
//
//

widget_svg_t
widget_svg_create(struct svg * svg_doc, bool is_srgb);

//
//
//

void
widget_svg_center(widget_svg_t            svg,  //
                  struct widget_control * control,
                  VkExtent2D const *      extent,
                  float                   cx,
                  float                   cy,
                  float                   scale);
//
//
//

void
widget_svg_rotate(widget_svg_t svg, struct widget_control * control, float theta);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_INCLUDE_WIDGET_SVG_H_
