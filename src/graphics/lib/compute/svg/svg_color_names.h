// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_COLOR_NAMES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_COLOR_NAMES_H_

//
//
//

#include "svg.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

#define SVG_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

//
//
//

struct svg_color_name
{
  char const * name;
  svg_color_t  color;
};

//
//
//

struct svg_color_name const *
svg_color_name_lookup(char const * str, uint32_t len);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SVG_SVG_COLOR_NAMES_H_

//
//
//
