// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_SVG_ARC_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_SVG_ARC_H_

//
//
//

#include "spinel/ext/geometry/arc.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Convert an arc from endpoint to center parameterization as defined in
// the SVG 1.1 spec.
//
// SVG arc requirements and implementation notes are described in detail
// in the W3C SVG specification.
//
// See SVG 1.1 / Section F.6: "Elliptical arc implementation notes".
//

void
spn_svg_arc(float              x0,
            float              y0,
            float              x1,
            float              y1,
            float              rx,
            float              ry,
            float              x_axis_rotation_radians,
            bool               large_arc_flag,
            bool               sweep_flag,
            spn_arc_params_t * arc_params);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_SVG_ARC_H_
