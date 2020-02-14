// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_ELLIPSE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_ELLIPSE_H_

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
// Add an ellipse
//

spn_result_t
spn_path_builder_ellipse(spn_path_builder_t path_builder,  //
                         float              cx,
                         float              cy,
                         float              rx,
                         float              ry);
//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_GEOMETRY_ELLIPSE_H_
