// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_spinel_image.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tests/common/scoped_struct.h"
#include "tests/common/spinel/spinel_test_utils.h"
#include "tests/common/svg/scoped_svg.h"
#include "tests/mock_spinel/mock_spinel_test_utils.h"

class SvgSpinelImageTest : public mock_spinel::Test {
};

TEST_F(SvgSpinelImageTest, SimpleTest)
{
  static const char kSvgData[] = "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
                                 "  <g style = \"fill: #FF0000\">\n"
                                 "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
                                 "  </g>\n"
                                 "</svg>\n";

  ScopedSvg svg = ScopedSvg::parseText(kSvgData);

  ScopedStruct<SvgSpinelImage> image(svg.get(), context_);

  image->setupPaths();

  // This performs a 45 degree rotation + sqrt(2) scale + translation by (4,4)
  const spn_transform_t kTestTransform = {
    .sx  = 2.,
    .shx = -2,
    .tx  = 4.,
    .shy = 2.,
    .sy  = 2.,
    .ty  = 4.,
  };
  image->setupRasters(&kTestTransform);
  image->setupLayers();

  // Check the data so far.
  {
    mock_spinel::Context * context = mock_context();

    const spn_path_t *   path_handles   = image->paths();
    const spn_raster_t * raster_handles = image->rasters();

    ASSERT_TRUE(path_handles);
    ASSERT_NE(path_handles[0].handle, UINT32_MAX);

    ASSERT_TRUE(raster_handles);
    ASSERT_NE(raster_handles[0].handle, UINT32_MAX);

    // Check that there is one single path.
    const auto & paths = context->paths();
    ASSERT_EQ(paths.size(), 1u);
    // clang-format off
    static const float kExpectedPathCoords[] = {
      MOCK_SPINEL_PATH_MOVE_TO_LITERAL(0, 0),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 0),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 16),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(0, 16),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(0, 0),
    };
    // clang-format on
    EXPECT_THAT(paths[0].data, ::testing::ElementsAreArray(kExpectedPathCoords));

    // Check there is a single raster, with a single path in it.
    const auto & rasters = context->rasters();
    ASSERT_EQ(rasters.size(), 1u);
    const auto & raster = rasters[0];
    ASSERT_EQ(raster.size(), 1u);
    const auto & rasterPath = raster[0];
    EXPECT_EQ(rasterPath.path_id, path_handles[0].handle);

    // This is the same as kTestTransform, except that everything is scaled by 32,
    // since this is the default scale factor, matching the Spinel sub-pixel space.
    const spn_transform_t kExpectedTransform = {
      .sx  = 64.,
      .shx = -64.,
      .tx  = 128.,
      .shy = 64.,
      .sy  = 64.,
      .ty  = 128.,
    };
    EXPECT_SPN_TRANSFORM_EQ(rasterPath.transform, kExpectedTransform);

    EXPECT_SPN_CLIP_EQ(rasterPath.clip, ::spinel_constants::default_clip);

    // Check the composition: there should be a single layer group, with a single layer in it.
    auto layerMap = mock_spinel::Composition::fromSpinel(image->composition)->computeLayerMap();
    ASSERT_EQ(layerMap.size(), 1u);
    const uint32_t id0 = 0u;

    ASSERT_EQ(layerMap.count(id0), 1u);
    const auto & layer0 = layerMap[id0];
    ASSERT_EQ(layer0.size(), 1u);
    EXPECT_EQ(layer0[0]->raster_id, raster_handles[0].handle);
    EXPECT_EQ(layer0[0]->layer_id, id0);
    EXPECT_EQ(layer0[0]->translation.tx, 0);
    EXPECT_EQ(layer0[0]->translation.ty, 0);

    // Minimal styling checks. The commands are implementation details at this
    // point, so don't bother checking them.
    const auto * styling = mock_spinel::Styling::fromSpinel(image->styling);
    ASSERT_EQ(styling->groups().size(), 1u);
    const auto & group = styling->groups()[0];
    EXPECT_EQ(group.layer_lo, 0u);
    EXPECT_EQ(group.layer_hi, 0u);
    EXPECT_FALSE(group.begin_commands.empty());
    EXPECT_FALSE(group.end_commands.empty());
    const auto & commandsMap = group.layer_commands;
    EXPECT_EQ(commandsMap.size(), 1u);
    const auto it = commandsMap.find(id0);
    ASSERT_NE(it, commandsMap.end());
    EXPECT_FALSE(it->second.empty());
  }
}
