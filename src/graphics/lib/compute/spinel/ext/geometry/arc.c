// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "arc.h"

#include <math.h>

#include "spinel/spinel_assert.h"

//
//
//

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//
// For now, just use a single maximum sweep size.
//

// clang-format off
#define SPN_SWEEP_RADIANS (2.0 * M_PI / 3.0)  // 120°
#define SPN_SWEEP_COS_2   0.5f                // cosf(120° / 2.0)
#define SPN_SWEEP_K       0.5f                // 2.0 * 0.5 * 0.5
// clang-format on

//
// Given a valid center parameterization emit zero or more rational
// quads of fixed sweep and one final arc of the remaining sweep.
//
spn_result_t
spn_path_builder_arc(spn_path_builder_t path_builder, spn_arc_params_t const * arc_params)
{
  //
  // cursory tests for a degenerate arc
  //

  // emit nothing
  if ((arc_params->x0 == arc_params->x1) && (arc_params->y0 == arc_params->y1))
    {
      return SPN_SUCCESS;
    };

  // emit a line
  if ((arc_params->theta_delta == 0.0f) || (arc_params->rx == 0.0f) || (arc_params->ry == 0.0f))
    {
      spn_path_builder_line_to(path_builder, arc_params->x1, arc_params->y1);

      return SPN_SUCCESS;
    }

  //
  // otherwise, emit rationals
  //
  float const cos_phi = cosf(arc_params->phi);
  float const sin_phi = sinf(arc_params->phi);

  float theta       = arc_params->theta;
  float theta_delta = arc_params->theta_delta;

  float x0 = arc_params->x0;
  float y0 = arc_params->y0;

  //
  // assume we're going to start with a full sweep
  //
  float const theta_sweep = theta_delta > 0.0f ? SPN_SWEEP_RADIANS : -SPN_SWEEP_RADIANS;

  while (true)
    {
      float const abs_theta_delta = fabs(theta_delta);
      bool const  is_final        = (abs_theta_delta <= SPN_SWEEP_RADIANS);

      float w1;
      float k;
      float xn;
      float yn;

      if (is_final)
        {
          w1 = cosf(theta_delta / 2.0f);
          k  = 2.0f * w1 * w1;

          xn = arc_params->x1;
          yn = arc_params->y1;
        }
      else
        {
          // clang-format off
          theta       += theta_sweep;
          theta_delta -= theta_sweep;
          // clang-format on

          w1 = SPN_SWEEP_COS_2;
          k  = SPN_SWEEP_K;

          float const rx_n = arc_params->rx * cosf(theta);
          float const ry_n = arc_params->ry * sinf(theta);

          xn = rx_n * cos_phi - ry_n * sin_phi + arc_params->cx;
          yn = rx_n * sin_phi + ry_n * cos_phi + arc_params->cy;
        }

      //
      // calculate the control point using A. Cantóna method
      //
      float const xc = (arc_params->cx * (k - 2.0f) + x0 + xn) / k;
      float const yc = (arc_params->cy * (k - 2.0f) + y0 + yn) / k;

      // emit the rat quad
      spn_result_t err;

      // FIXME(allanmac): eventually migrate this to the non-relative path builder
      err = spn_path_builder_rat_quad_to(path_builder, xc, yc, xn, yn, w1);

      if (err != SPN_SUCCESS)
        return err;

      if (is_final)
        return SPN_SUCCESS;

      // otherwise
      x0 = xn;
      y0 = yn;
    }
}

//
//
//
