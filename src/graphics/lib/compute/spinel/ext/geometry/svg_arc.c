// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "svg_arc.h"

#include <math.h>

//
//
//

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//
// Angle of vector relative to (1,0)
//
static float
spn_angle_x(float const x, float const y)
{
  float const len = sqrtf(x * x + y * y);

  if (len > 0.0f)
    {
      float principal_angle = acosf(x / len);

      if (y < 0.0f)
        {
          principal_angle = -principal_angle;
        }

      return principal_angle;
    }
  else
    {
      return 0.0f;
    }
}

//
// SVG arc requirements and implementation notes are described in detail
// in the W3C SVG specification.
//
// The strategy used here is to perform the steps described in the SVG
// spec for converting from endpoint to center point parameterization.
//
// See SVG 1.1 / Section F.6: "Elliptical arc implementation notes"
//
// FIXME(allanmac): There is likely a more succinct approach using
// geometric characteristics of an ellipse. See the following sources:
//
//   * "Geometric characteristics of conics in Bézier form"
//     A. Cantóna, L. Fernández-Jambrina, E. Rosado María
//
//   * "The NURBS Book", Les Piegl and Wayne Tiller
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
            spn_arc_params_t * arc_params)
{
  // initialize arc parameter block
  *arc_params = (spn_arc_params_t){ .x0 = x0,  //
                                    .y0 = y0,
                                    .x1 = x1,
                                    .y1 = y1,
                                    .rx = rx,
                                    .ry = ry };

  // omit the arc
  if ((x0 == x1) && (y0 == y1))
    return;

  // out-of-range parameters: radii zero
  if ((rx == 0.0f) || (ry == 0.0f))
    return;

  // out-of-range parameters: radii negative
  arc_params->rx = fabsf(rx);
  arc_params->ry = fabsf(ry);

  // out-of-range parameters: x axis rotation
  arc_params->phi = fmodf(x_axis_rotation_radians, M_PI * 2.0);

  // reuse cos/sin
  float const cos_phi = cosf(arc_params->phi);
  float const sin_phi = sinf(arc_params->phi);

  // move origin to midpoint of P0P1
  float const ox = (x0 - x1) * 0.5f;
  float const oy = (y0 - y1) * 0.5f;

  // adjust origin for x axis rotation
  float const nx = ox * cos_phi + oy * sin_phi;
  float const ny = oy * cos_phi - ox * sin_phi;

  // out-of-range parameters: radii too small
  float const rxrx = rx * rx;
  float const ryry = ry * ry;

  float const nxnx = nx * nx;
  float const nyny = ny * ny;

  float const delta = (nxnx / rxrx) + (nyny / ryry);

  // center point defaults to midpoint of chord P0P1
  arc_params->cx = (x0 + x1) * 0.5f;
  arc_params->cy = (y0 + y1) * 0.5f;

  //
  // based on the radii scaling, compute the transformed center point
  //
  float v0x;
  float v0y;

  float v1x;
  float v1y;

  if (delta <= 1.0f)
    {
      float const rad_numer = rxrx * ryry - rxrx * nyny - ryry * nxnx;
      float const rad_denom = rxrx * nyny + ryry * nxnx;
      float const rad       = rad_numer / rad_denom;
      float       rad_sqrt  = sqrtf(rad);

      if (large_arc_flag == sweep_flag)
        {
          rad_sqrt = -rad_sqrt;
        }

      float const ex = +rad_sqrt * rx * ny / ry;
      float const ey = -rad_sqrt * ry * nx / rx;

      v0x = (+nx - ex) / rx;
      v0y = (+ny - ey) / ry;

      v1x = (-nx - ex) / rx;
      v1y = (-ny - ey) / ry;

      arc_params->cx += ex * cos_phi - ey * sin_phi;
      arc_params->cy += ex * sin_phi + ey * cos_phi;
    }
  else
    {
      float const delta_sqrt = sqrtf(delta);

      rx *= delta_sqrt;
      ry *= delta_sqrt;

      v0x = +nx / rx;
      v0y = +ny / ry;

      v1x = -nx / rx;
      v1y = -ny / ry;
    }

  // compute theta angles
  arc_params->theta       = spn_angle_x(v0x, v0y);
  arc_params->theta_delta = spn_angle_x(v1x, v1y) - arc_params->theta;

  // adjust for sweep flag
  if (sweep_flag)
    {
      if (arc_params->theta_delta < 0.0f)
        {
          arc_params->theta_delta += (float)(M_PI * 2.0);
        }
    }
  else
    {
      if (arc_params->theta_delta > 0.0)
        {
          arc_params->theta_delta -= (float)(M_PI * 2.0);
        }
    }
}

//
//
//
