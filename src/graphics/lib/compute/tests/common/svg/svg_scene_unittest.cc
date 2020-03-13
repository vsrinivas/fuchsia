// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/svg/svg_scene.h"

#include <gtest/gtest.h>

#include "scoped_svg.h"
#include "tests/common/affine_transform_test_utils.h"

TEST(SvgSceneTest, SingleSvg)
{
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <rect x=\"10\" y=\"20\" width=\"100\" height=\"50\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  SvgScene scene;
  scene.addSvgDocument(svg.get(), 200., 300.);

  double xmin, ymin, xmax, ymax;
  scene.getBounds(&xmin, &ymin, &xmax, &ymax);
  EXPECT_DOUBLE_EQ(xmin, 210.);
  EXPECT_DOUBLE_EQ(ymin, 320.);
  EXPECT_DOUBLE_EQ(xmax, 310.);
  EXPECT_DOUBLE_EQ(ymax, 370.);

  const auto & svgs = scene.unique_svgs();
  ASSERT_EQ(svgs.size(), 1u);
  EXPECT_EQ(svgs[0], svg.get());

  const auto & paths = scene.unique_paths();
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0].svg_index, 0u);
  EXPECT_EQ(paths[0].path_id, 0u);

  const auto & rasters = scene.unique_rasters();
  ASSERT_EQ(rasters.size(), 1u);
  EXPECT_EQ(rasters[0].svg_index, 0u);
  EXPECT_EQ(rasters[0].raster_id, 0u);
  EXPECT_EQ(rasters[0].path_index, 0u);

  EXPECT_AFFINE_TRANSFORM_EQ(rasters[0].transform, affine_transform_make_translation(200., 300.));

  const auto & layers = scene.layers();
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].svg_index, 0u);
  EXPECT_EQ(layers[0].layer_id, 0u);
  EXPECT_EQ(layers[0].fill_color, 0xff0000u);
  EXPECT_DOUBLE_EQ(layers[0].fill_opacity, 1.0);
  EXPECT_FALSE(layers[0].fill_even_odd);
  EXPECT_DOUBLE_EQ(layers[0].opacity, 1.0);
  ASSERT_EQ(layers[0].prints.size(), 1u);
  EXPECT_EQ(layers[0].prints[0].raster_index, 0u);
  EXPECT_EQ(layers[0].prints[0].tx, 0);
  EXPECT_EQ(layers[0].prints[0].ty, 0);
}

TEST(SvgSceneTest, RepeatedSvg)
{
  // Same SVG document repeated multiple times on the scene with different
  // transforms.
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <rect x=\"10\" y=\"20\" width=\"100\" height=\"50\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  SvgScene scene;

  const size_t kCount      = 10;
  const double kTranslateX = 20.;
  const double kTranslateY = 60.;

  for (size_t nn = 0; nn < kCount; ++nn)
    scene.addSvgDocument(svg.get(), nn * kTranslateX, nn * kTranslateY);

  double xmin, ymin, xmax, ymax;
  scene.getBounds(&xmin, &ymin, &xmax, &ymax);
  EXPECT_DOUBLE_EQ(xmin, 10.);
  EXPECT_DOUBLE_EQ(ymin, 20.);
  EXPECT_DOUBLE_EQ(xmax, 110. + kTranslateX * (kCount - 1));
  EXPECT_DOUBLE_EQ(ymax, 70. + kTranslateY * (kCount - 1));

  const auto & svgs = scene.unique_svgs();
  ASSERT_EQ(svgs.size(), 1u);
  EXPECT_EQ(svgs[0], svg.get());

  const auto & paths = scene.unique_paths();
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0].svg_index, 0u);
  EXPECT_EQ(paths[0].path_id, 0u);

  const auto & rasters = scene.unique_rasters();
  ASSERT_EQ(rasters.size(), kCount);
  for (size_t nn = 0; nn < kCount; ++nn)
    {
      std::string  text   = std::string("raster") + std::to_string(nn);
      const auto & raster = rasters[nn];
      EXPECT_EQ(raster.svg_index, 0u) << text;
      EXPECT_EQ(raster.raster_id, 0u) << text;
      EXPECT_EQ(raster.path_index, 0u) << text;

      EXPECT_AFFINE_TRANSFORM_EQ(
        raster.transform,
        affine_transform_make_translation(nn * kTranslateX, nn * kTranslateY));
    }

  const auto & layers = scene.layers();
  ASSERT_EQ(layers.size(), kCount);
  for (size_t nn = 0; nn < kCount; ++nn)
    {
      std::string  text  = std::string("layer") + std::to_string(nn);
      const auto & layer = layers[nn];

      EXPECT_EQ(layer.svg_index, 0u) << text;
      EXPECT_EQ(layer.layer_id, nn) << text;
      EXPECT_EQ(layer.fill_color, 0xff0000u) << text;
      EXPECT_DOUBLE_EQ(layer.fill_opacity, 1.0) << text;
      EXPECT_FALSE(layer.fill_even_odd) << text;
      EXPECT_DOUBLE_EQ(layer.opacity, 1.0) << text;
      ASSERT_EQ(layer.prints.size(), 1u) << text;
      EXPECT_EQ(layer.prints[0].raster_index, nn) << text;
      EXPECT_EQ(layer.prints[0].tx, 0) << text;
      EXPECT_EQ(layer.prints[0].ty, 0) << text;
    }
}

TEST(SvgSceneTest, MultipleSvgs)
{
  // Same SVG document repeated multiple times on the scene with different
  // transforms.

  // clang-format off
  ScopedSvg svg1 = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <rect x=\"10\" y=\"20\" width=\"100\" height=\"50\" />"
    "  </g>"
    "</svg>"
  );
  ScopedSvg svg2 = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #0000FF\">"
    "    <path d=\"M 10 20 l 100 25 l -100 25 Z\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  const size_t kGridWidth  = 8;
  const size_t kGridHeight = 4;
  const size_t kCount      = kGridWidth * kGridHeight;
  const double kTranslateX = 20.;
  const double kTranslateY = 60.;

  SvgScene scene;
  for (size_t nn = 0; nn < kCount; ++nn)
    {
      // Organize items in a grid, alternating each instance between svg1 and
      // svg2, so it looks like:
      //
      //    X O X O X O
      //    O X O X O X
      //    X O X O X O
      //    ...
      size_t grid_y = nn / kGridWidth;
      size_t grid_x = nn % kGridWidth;

      const svg * item = (nn & 1) ? svg2.get() : svg1.get();

      scene.addSvgDocument(
        item,
        affine_transform_make_translation(grid_x * kTranslateX, grid_y * kTranslateY));
    }

  double xmin, ymin, xmax, ymax;
  scene.getBounds(&xmin, &ymin, &xmax, &ymax);
  EXPECT_DOUBLE_EQ(xmin, 10.);
  EXPECT_DOUBLE_EQ(ymin, 20.);
  EXPECT_DOUBLE_EQ(xmax, 110. + kTranslateX * (kGridWidth - 1));
  EXPECT_DOUBLE_EQ(ymax, 70. + kTranslateY * (kGridHeight - 1));

  const auto & svgs = scene.unique_svgs();
  ASSERT_EQ(svgs.size(), 2u);
  EXPECT_EQ(svgs[0], svg1.get());
  EXPECT_EQ(svgs[1], svg2.get());

  const auto & paths = scene.unique_paths();
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0].svg_index, 0u);
  EXPECT_EQ(paths[0].path_id, 0u);
  EXPECT_EQ(paths[1].svg_index, 1u);
  EXPECT_EQ(paths[1].path_id, 0u);

  const auto & rasters = scene.unique_rasters();
  ASSERT_EQ(rasters.size(), kCount);
  for (size_t nn = 0; nn < kCount; ++nn)
    {
      std::string text   = std::string("raster") + std::to_string(nn);
      size_t      grid_x = nn % kGridWidth;
      size_t      grid_y = nn / kGridWidth;

      const auto & raster = rasters[nn];
      EXPECT_EQ(raster.svg_index, (nn & 1u)) << text;
      EXPECT_EQ(raster.raster_id, 0u) << text;
      EXPECT_EQ(raster.path_index, (nn & 1u)) << text;

      EXPECT_AFFINE_TRANSFORM_EQ(
        raster.transform,
        affine_transform_make_translation(grid_x * kTranslateX, grid_y * kTranslateY));
    }

  const auto & layers = scene.layers();
  ASSERT_EQ(layers.size(), kCount);
  for (size_t nn = 0; nn < kCount; ++nn)
    {
      std::string  text  = std::string("layer") + std::to_string(nn);
      const auto & layer = layers[nn];

      EXPECT_EQ(layer.svg_index, (nn & 1u)) << text;
      EXPECT_EQ(layer.layer_id, nn) << text;
      EXPECT_EQ(layer.fill_color, (nn & 1u) ? 0x000000ffu : 0xff0000u) << text;
      EXPECT_DOUBLE_EQ(layer.fill_opacity, 1.0) << text;
      EXPECT_FALSE(layer.fill_even_odd) << text;
      EXPECT_DOUBLE_EQ(layer.opacity, 1.0) << text;
      ASSERT_EQ(layer.prints.size(), 1u) << text;
      EXPECT_EQ(layer.prints[0].raster_index, nn) << text;
      EXPECT_EQ(layer.prints[0].tx, 0) << text;
      EXPECT_EQ(layer.prints[0].ty, 0) << text;
    }
}
