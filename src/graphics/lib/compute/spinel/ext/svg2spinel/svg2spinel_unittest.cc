// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg2spinel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "spinel/spinel_opcodes.h"
#include "tests/common/spinel/spinel_test_utils.h"
#include "tests/common/utils.h"  // For ARRAY_SIZE() macro.
#include "tests/mock_spinel/mock_spinel_test_utils.h"

namespace {

class Svg2SpinelTest : public mock_spinel::Test {
};

class ScopedSvg {
 public:
  ScopedSvg(const char * doc) : svg_(svg_parse(doc, false))
  {
  }

  ~ScopedSvg()
  {
    if (svg_)
      svg_dispose(svg_);
  }

  svg *
  get() const
  {
    return svg_;
  }

 private:
  svg * svg_;
};

}  // namespace

//
//
//

TEST_F(Svg2SpinelTest, polyline)
{
  char const doc[] = { "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
                       "  <g style = \"fill: #FF0000\">\n"
                       "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
                       "  </g>\n"
                       "</svg>\n" };

  ScopedSvg svg(doc);
  ASSERT_TRUE(svg.get());

  uint32_t const layer_count = svg_layer_count(svg.get());
  EXPECT_EQ(layer_count, 1u);

  // Verify path coordinates.
  spn_path_t * paths = spn_svg_paths_decode(svg.get(), path_builder_);
  ASSERT_TRUE(paths);
  {
    const auto & paths = mock_context()->paths();
    ASSERT_EQ(paths.size(), 1u);
    const auto & path = paths[0];
    // clang-format off
    static const float kExpectedPath[] = {
      MOCK_SPINEL_PATH_MOVE_TO_LITERAL(0,   0),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16,  0),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 16),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(0,  16),
      MOCK_SPINEL_PATH_LINE_TO_LITERAL(0,   0),
    };
    // clang-format on
    ASSERT_THAT(path.data, ::testing::ElementsAreArray(kExpectedPath));
  }
  // Verify raster stack.
  transform_stack * ts = transform_stack_create(32);
  transform_stack_push_identity(ts);

  spn_raster_t * rasters = spn_svg_rasters_decode(svg.get(), raster_builder_, paths, ts);
  ASSERT_TRUE(rasters);

  {
    const auto & rasters = mock_context()->rasters();
    ASSERT_EQ(rasters.size(), 1u);
    const auto & raster = rasters[0];

    ASSERT_EQ(raster.size(), 1u);
    const auto & raster_path = raster[0];
    EXPECT_EQ(raster_path.path_id, paths[0].handle);
    EXPECT_SPN_TRANSFORM_IS_IDENTITY(raster_path.transform);
  }

  // Verify composition and layers
  spn_svg_layers_decode(svg.get(), rasters, composition_, styling_, true);

  {
    const auto & prints = mock_composition()->prints();
    ASSERT_EQ(prints.size(), 1u);
    const auto & print = prints[0];
    EXPECT_EQ(print.raster_id, rasters[0].handle);
    EXPECT_EQ(print.layer_id, 0u);
    const spn_txty_t zero_translation = { 0, 0 };
    EXPECT_SPN_TXTY_EQ(print.translation, zero_translation);

    const auto layerMap = mock_composition()->computeLayerMap();
    ASSERT_EQ(layerMap.size(), 1u);
    auto const it = layerMap.find(0u);
    ASSERT_TRUE(it != layerMap.end());
    EXPECT_EQ(it->first, 0u);
    EXPECT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0], &print);
  }

  {
    const auto & groups = mock_styling()->groups();
    ASSERT_EQ(groups.size(), 1u);
    const auto & group = groups[0];
    const auto   it    = group.layer_commands.find(0u);
    ASSERT_TRUE(it != group.layer_commands.end());
    const auto & commands = it->second;

    uint32_t kExpectedCommands[] = {
      static_cast<uint32_t>(SPN_STYLING_OPCODE_COVER_NONZERO),
      static_cast<uint32_t>(SPN_STYLING_OPCODE_COLOR_FILL_SOLID),
      0,  // filled later.
      0,  // filled later.
      static_cast<uint32_t>(SPN_STYLING_OPCODE_BLEND_OVER),
      static_cast<uint32_t>(SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY),
    };
    static const float kRedRgba[4] = { 1., 0., 0., 1. };
    mock_spinel::Spinel::rgbaToCmds(kRedRgba, kExpectedCommands + 2);

    ASSERT_THAT(commands, ::testing::ElementsAreArray(kExpectedCommands));
  }

  transform_stack_release(ts);

  spn_svg_rasters_release(svg.get(), context_, rasters);
  spn_svg_paths_release(svg.get(), context_, paths);
}
