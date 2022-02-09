// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_ROBOTO_MONO_REGULAR_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_ROBOTO_MONO_REGULAR_H_

//
//
//

#include "spinel/spinel.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct font_metrics
{
  int32_t ascent;
  int32_t descent;

  struct
  {
    int32_t width;
  } advance;
};

#define FONT_METRICS_PFN(f_) font_##f_##_metrics

#define FONT_METRICS_PROTO(f_) void FONT_METRICS_PFN(f_)(struct font_metrics * const metrics)

//
//
//

#define FONT_GLYPH_PFN(f_, g_) font_##f_##_##g_

#define FONT_GLYPH_PROTO(f_, g_)                                                                   \
  void FONT_GLYPH_PFN(f_, g_)(spinel_path_builder_t pb, spinel_path_t * path)

//
//
//

FONT_METRICS_PROTO(roboto_mono_regular);

FONT_GLYPH_PROTO(roboto_mono_regular, paren_left);
FONT_GLYPH_PROTO(roboto_mono_regular, paren_right);
FONT_GLYPH_PROTO(roboto_mono_regular, comma);
FONT_GLYPH_PROTO(roboto_mono_regular, zero);
FONT_GLYPH_PROTO(roboto_mono_regular, one);
FONT_GLYPH_PROTO(roboto_mono_regular, two);
FONT_GLYPH_PROTO(roboto_mono_regular, three);
FONT_GLYPH_PROTO(roboto_mono_regular, four);
FONT_GLYPH_PROTO(roboto_mono_regular, five);
FONT_GLYPH_PROTO(roboto_mono_regular, six);
FONT_GLYPH_PROTO(roboto_mono_regular, seven);
FONT_GLYPH_PROTO(roboto_mono_regular, eight);
FONT_GLYPH_PROTO(roboto_mono_regular, nine);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_WIDGET_ROBOTO_MONO_REGULAR_H_
