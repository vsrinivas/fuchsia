// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "ellipse.h"

#include "spinel/spinel_assert.h"

//
// Not a constant found in <math.h>
//

#ifndef SPN_M_SQRT3_2

#define SPN_M_SQRT3_2 0.86602540378443864676 // sqrt(3)/2

#endif

//
//
//

// clang-format off
#define SPN_SWEEP_COS_2    0.5f                 // cosf(120° / 2.0)
#define SPN_SWEEP_SIN_2    (float)SPN_M_SQRT3_2 // sinf(120° / 2.0)
#define SPN_SWEEP_K        0.5f                 // 2.0 * 0.5 * 0.5
#define SPN_SWEEP_K_INV    2.0f                 // 1 / 0.5
// clang-format on

//
//
//

#define SPN_ELLIPSE_SWEEP(x1_, y1_, x2_, y2_)                                                      \
  if ((err = spn_path_builder_rat_quad_to(path_builder, /* */                                      \
                                          x1_,                                                     \
                                          y1_,                                                     \
                                          x2_,                                                     \
                                          y2_,                                                     \
                                          SPN_SWEEP_COS_2)) != SPN_SUCCESS)                        \
    return err;

//
// Draw an ellipse with 3 rational quads
//
spn_result_t
spn_path_builder_ellipse(spn_path_builder_t path_builder,  //
                         float              cx,
                         float              cy,
                         float              rx,
                         float              ry)
{
  //
  // calculate 3 end points
  //
  float const px0 = cx + rx;
  float const py0 = cy;

  float const pxE = cx - rx * SPN_SWEEP_COS_2;
  float const py1 = cy + ry * SPN_SWEEP_SIN_2;

  // px2 = px1
  float const py2 = cy - ry * SPN_SWEEP_SIN_2;

  //
  // calculate 3 control points using A. Cantóna method
  //
  float const qx0 = (cx * (SPN_SWEEP_K - 2.0f) + px0 + pxE) * SPN_SWEEP_K_INV;
  float const qy0 = (cy * (SPN_SWEEP_K - 2.0f) + py0 + py1) * SPN_SWEEP_K_INV;

  float const qx1 = (cx * (SPN_SWEEP_K - 2.0f) + pxE + pxE) * SPN_SWEEP_K_INV;
  float const qy1 = (cy * (SPN_SWEEP_K - 2.0f) + py1 + py2) * SPN_SWEEP_K_INV;

  float const qx2 = (cx * (SPN_SWEEP_K - 2.0f) + pxE + px0) * SPN_SWEEP_K_INV;
  float const qy2 = (cy * (SPN_SWEEP_K - 2.0f) + py2 + py0) * SPN_SWEEP_K_INV;

  //
  // emit 3 arc control cages
  //
  spn_path_builder_move_to(path_builder, px0, py0);

  spn_result_t err;

  SPN_ELLIPSE_SWEEP(qx0, qy0, pxE, py1);
  SPN_ELLIPSE_SWEEP(qx1, qy1, pxE, py2);
  SPN_ELLIPSE_SWEEP(qx2, qy2, px0, py0);

  return SPN_SUCCESS;
}

//
//
//
