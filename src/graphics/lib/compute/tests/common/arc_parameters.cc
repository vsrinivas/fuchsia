// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/arc_parameters.h"

#include <math.h>

// All computations taken from http://www.w3.org/TR/SVG/implnote.html

arc_endpoint_parameters_t
arc_endpoint_parameters_from_center(arc_center_parameters_t params)
{
  // Section B.2.3. Conversion from center to endpoint parameterization
  const double cos_phi = cos(params.phi);
  const double sin_phi = sin(params.phi);

  const double org_x1 = params.rx * cos(params.theta);
  const double org_y1 = params.ry * sin(params.theta);

  const double org_x2 = params.rx * cos(params.theta + params.theta_delta);
  const double org_y2 = params.ry * sin(params.theta + params.theta_delta);

  return {
    .x1 = params.cx + cos_phi * org_x1 - sin_phi * org_y1,
    .y1 = params.cy + sin_phi * org_x1 + cos_phi * org_y1,

    .x2 = params.cx + cos_phi * org_x2 - sin_phi * org_y2,
    .y2 = params.cy + sin_phi * org_x2 + cos_phi * org_y2,

    .large_arc_flag = fabs(params.theta_delta) > M_PI,
    .sweep_flag     = params.theta_delta > 0,

    .rx  = params.rx,
    .ry  = params.ry,
    .phi = params.phi,
  };
}

static double
angle_from(double dx, double dy)
{
  double len = hypot(dx, dy);
  if (!isnormal(len))
    return 0.;

  double angle = acos(dx / len);
  return dy < 0 ? -angle : angle;
}

// Compute the endpoint parameterization of a given arc from its center one.
arc_center_parameters_t
arc_center_parameters_from_endpoint(arc_endpoint_parameters_t params)
{
  // Section C.4.2. Out-of-range parameters

  // "If the endpoints [...] are identical, then this is equivalent to omitting
  // the elliptic arc segment entirely."
  if (params.x2 == params.x1 && params.y2 == params.y1)
    return {};

  // B.2.5 step2 (Ensure radii are positive).
  double rx = fabs(params.rx);
  double ry = fabs(params.ry);

  // B.2.5 step 1 (Ensure radii are non zero).
  if (rx == 0. || ry == 0.)
    return {};

  // B.2.4 step1 (Equation 5.1)
  const double cos_phi = cos(params.phi);
  const double sin_phi = sin(params.phi);

#if 1
  // NOTE: The following computations are equivalent to the one specified
  // by the SVG implementation note (and found below in the #else .. #endif
  // block). Experimentation / debugging shows that they give the same result
  // up to the 14th decimal, and that this version seems to have slightly less
  // rounding errors overall.

  // Undo axis rotation and radii scaling first.
  const double x1 = (+params.x1 * cos_phi + params.y1 * sin_phi) / rx;
  const double y1 = (-params.x1 * sin_phi + params.y1 * cos_phi) / ry;

  const double x2 = (+params.x2 * cos_phi + params.y2 * sin_phi) / rx;
  const double y2 = (-params.x2 * sin_phi + params.y2 * cos_phi) / ry;

  // Points are now on a unit circle, find its center in transformed space.
  const double lx = (x1 - x2) * 0.5;
  const double ly = (y1 - y2) * 0.5;

  double cx = (x1 + x2) * 0.5;
  double cy = (y1 + y2) * 0.5;

  const double llen2 = lx * lx + ly * ly;
  if (llen2 < 1)
    {
      double radicand = sqrt((1 - llen2) / llen2);
      if (params.large_arc_flag != params.sweep_flag)
        radicand = -radicand;

      cx += -ly * radicand;
      cy += +lx * radicand;
    }

  // Get angle and angle sweep.
  double theta       = angle_from(x1 - cx, y1 - cy);
  double theta_delta = angle_from(x2 - cx, y2 - cy) - theta;

  // convert center coordinates back to original space.
  const double cxs = cx * rx;
  const double cys = cy * ry;

  cx = cxs * cos_phi - cys * sin_phi;
  cy = cxs * sin_phi + cys * cos_phi;

#else   // !1
  const double mx = (params.x1 - params.x2) * 0.5;
  const double my = (params.y1 - params.y2) * 0.5;

  const double x1p = cos_phi * mx + sin_phi * my;
  const double y1p = cos_phi * my - sin_phi * mx;

  // B.5.2. step 3 (Ensure radii are large enough).
  double rxrx = rx * rx;
  double ryry = ry * ry;

  const double x1px1p = x1p * x1p;
  const double y1py1p = y1p * y1p;

  // Starting values for cx', cy', cx and cy, corresponding to the case
  // where |sigma > 1| below.
  double cxp = 0.;
  double cyp = 0.;
  double cx  = (params.x1 + params.x2) * 0.5;
  double cy  = (params.y1 + params.y2) * 0.5;

  const double sigma = x1px1p / rxrx + y1py1p / ryry;
  if (sigma >= 1.0)
    {
      const double sigma_sqrt = sqrt(sigma);
      rx                      = sigma_sqrt * rx;
      ry                      = sigma_sqrt * ry;
    }
  else
    {
      // Back to Section B.2.4: Equation 5.2
      const double aa = rxrx * y1py1p;
      const double bb = ryry * x1px1p;

      double radicand = sqrt((rxrx * ryry - aa - bb) / (aa + bb));
      if (params.large_arc_flag == params.sweep_flag)
        radicand = -radicand;

      cxp = radicand * rx * y1p / ry;
      cyp = -radicand * ry * x1p / rx;

      // B.5.2. step 3 Compute center.
      cx += cos_phi * cxp - sin_phi * cyp;
      cy += sin_phi * cxp + cos_phi * cyp;
    }

  // B.5.2. step 4 (Compute theta1 and Dtheta)

  double theta       = angle_from((x1p - cxp) / rx, (y1p - cyp) / ry);
  double theta_delta = angle_from(-(x1p + cxp) / rx, -(y1p + cyp) / ry) - theta;
#endif  // !1

  if (params.sweep_flag)
    {
      if (theta_delta < 0.)
        theta_delta += M_PI * 2.;
    }
  else
    {
      if (theta_delta > 0.)
        theta_delta -= M_PI * 2;
    }

  return {
    .cx          = cx,
    .cy          = cy,
    .rx          = rx,
    .ry          = ry,
    .phi         = params.phi,
    .theta       = theta,
    .theta_delta = theta_delta,
  };
}
