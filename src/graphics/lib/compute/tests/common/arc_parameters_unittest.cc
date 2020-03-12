// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/arc_parameters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

static const struct
{
  const char *              desc;
  arc_center_parameters_t   center;
  arc_endpoint_parameters_t endpoints;
} kTestData[] = {
  // clang-format off
  {
    .desc = "Basic quarter unit circle",
    .center = {
      .cx = 0.,
      .cy = 0.,
      .rx = 1.,
      .ry = 1.,
      .phi = 0.,
      .theta = 0.,
      .theta_delta = M_PI / 2,
    },
    .endpoints = {
      .x1 = 1.,
      .y1 = 0.,
      .x2 = 0.,
      .y2 = 1.,
      .large_arc_flag = false,
      .sweep_flag     = true,
      .rx             = 1.,
      .ry             = 1.,
      .phi            = 0.,
    },
  },
  {
    .desc = "Reverse 3/4 unit circle",
    .center = {
      .cx = 0.,
      .cy = 0.,
      .rx = 1.,
      .ry = 1.,
      .phi = 0.,
      .theta = M_PI / 2,
      .theta_delta = - 3 * M_PI / 2,
    },
    .endpoints = {
      .x1 = 0.,
      .y1 = 1.,
      .x2 = -1.,
      .y2 = 0.,
      .large_arc_flag = true,
      .sweep_flag     = false,
      .rx             = 1.,
      .ry             = 1.,
      .phi            = 0.,
    },
  },
  {
    .desc = "30-degrees rotated ellipse, 160-degrees arc",
    .center = {
      .cx = 100.,
      .cy = 50.,
      .rx = 50.,
      .ry = 20.,
      .phi = M_PI / 6,
      .theta = M_PI / 6,
      .theta_delta = 160. * (M_PI / 180.),
    },
    .endpoints = {
      .x1 = 132.5,
      .y1 = 80.310889132455344,
      .x2 = 59.093055179047134,
      .y2 = 22.372131511086099,
      .large_arc_flag = false,
      .sweep_flag     = true,
      .rx             = 50.,
      .ry             = 20.,
      .phi            = M_PI / 6.,
    },
  },
  {
    .desc = "Same ellipse as above, same endpoints, but reverse large arc",
    .center = {
      .cx = 100.,
      .cy = 50.,
      .rx = 50.,
      .ry = 20.,
      .phi = M_PI / 6,
      .theta = M_PI / 6,
      .theta_delta = -200. * (M_PI / 180.),
    },
    .endpoints = {
      .x1 = 132.5,
      .y1 = 80.310889132455344,
      .x2 = 59.093055179047134,
      .y2 = 22.372131511086099,
      .large_arc_flag = true,
      .sweep_flag     = false,
      .rx             = 50.,
      .ry             = 20.,
      .phi            = M_PI / 6.,
    },
  },
  // clang-format on
};

TEST(ArcParameters, CenterToEndpoint)
{
  int counter = 0;

  for (const auto & data : kTestData)
    {
      std::string text = std::string("#") + std::to_string(++counter) + " " + data.desc;
      arc_endpoint_parameters_t output   = arc_endpoint_parameters_from_center(data.center);
      arc_endpoint_parameters_t expected = data.endpoints;
      double                    epsilon  = 1e-9;

      EXPECT_NEAR(output.x1, expected.x1, epsilon) << text;
      EXPECT_NEAR(output.y1, expected.y1, epsilon) << text;
      EXPECT_NEAR(output.x2, expected.x2, epsilon) << text;
      EXPECT_NEAR(output.y2, expected.y2, epsilon) << text;
      EXPECT_EQ(output.large_arc_flag, expected.large_arc_flag) << text;
      EXPECT_EQ(output.sweep_flag, expected.sweep_flag) << text;
      EXPECT_NEAR(output.rx, expected.rx, epsilon) << text;
      EXPECT_NEAR(output.ry, expected.ry, epsilon) << text;
      EXPECT_NEAR(output.phi, expected.phi, epsilon) << text;
    }
}

TEST(ArcParameters, EndpointToCenter)
{
  int counter = 0;

  for (const auto & data : kTestData)
    {
      std::string             text = std::string("#") + std::to_string(++counter) + " " + data.desc;
      arc_center_parameters_t output   = arc_center_parameters_from_endpoint(data.endpoints);
      arc_center_parameters_t expected = data.center;
      double                  epsilon  = 1e-9;

      EXPECT_NEAR(output.cx, expected.cx, epsilon) << text;
      EXPECT_NEAR(output.cy, expected.cy, epsilon) << text;
      EXPECT_NEAR(output.rx, expected.rx, epsilon) << text;
      EXPECT_NEAR(output.ry, expected.ry, epsilon) << text;
      EXPECT_NEAR(output.phi, expected.phi, epsilon) << text;
      EXPECT_NEAR(output.theta, expected.theta, epsilon) << text;
      EXPECT_NEAR(output.theta_delta, expected.theta_delta, epsilon) << text;
    }
}
