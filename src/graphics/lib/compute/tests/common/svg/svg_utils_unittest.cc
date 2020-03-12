// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_utils.h"

#include <gtest/gtest.h>

#include "svg/svg.h"
#include "tests/common/affine_transform_test_utils.h"
#include "tests/common/path_sink_test_utils.h"
#include "tests/common/svg/scoped_svg.h"

// clang-format off
static const char kSvg1[] = R"###(
<svg xmlns="http://www.w3.org/2000/svg" width="720.00586" height="720.00586" viewBox="0 0 720.00586 720.00586">
  <title>g-shock_nostroke</title>
  <g>
    <g>
      <circle cx="546.09291" cy="62.95645" r="33.96009"
              transform="translate(103.41615 388.86683) rotate(-42.93738)" fill="#414042"/>
    </g>
    <g>
      <circle cx="173.89922" cy="657.09922" r="33.96094"
              transform="translate(-230.42375 110.7132) rotate(-21.67799)" fill="#414042"/>
    </g>
  </g>
</svg>
)###";

// clang-format on

TEST(SvgUtils, DecodePath)
{
  ScopedSvg svg = ScopedSvg::parseString(kSvg1);

  ASSERT_TRUE(svg.get());

  {
    RecordingPathSink sink;
    EXPECT_TRUE(svg_decode_path(svg.get(), 0u, nullptr, &sink));
    EXPECT_STREQ(sink.to_string().c_str(),
                 "BEGIN;"
                 "MOVE_TO(580.053 62.9565);"
                 "RAT_QUAD_TO(580.053 96.9165 546.093 96.9165 0.707107);"
                 "RAT_QUAD_TO(512.133 96.9165 512.133 62.9565 0.707107);"
                 "RAT_QUAD_TO(512.133 28.9964 546.093 28.9964 0.707107);"
                 "RAT_QUAD_TO(580.053 28.9964 580.053 62.9565 0.707107);"
                 "END");
  }

  {
    RecordingPathSink sink;
    EXPECT_TRUE(svg_decode_path(svg.get(), 1u, nullptr, &sink));
    EXPECT_STREQ(sink.to_string().c_str(),
                 "BEGIN;"
                 "MOVE_TO(207.86 657.099);"
                 "RAT_QUAD_TO(207.86 691.06 173.899 691.06 0.707107);"
                 "RAT_QUAD_TO(139.938 691.06 139.938 657.099 0.707107);"
                 "RAT_QUAD_TO(139.938 623.138 173.899 623.138 0.707107);"
                 "RAT_QUAD_TO(207.86 623.138 207.86 657.099 0.707107);"
                 "END");
  }

  {
    std::vector<SvgDecodedRaster> rasters;
    auto                          callback = [&rasters](const SvgDecodedRaster & r) -> bool {
      rasters.push_back(r);
      return true;
    };

    EXPECT_TRUE(svg_decode_rasters(svg.get(), nullptr, callback));

    ASSERT_EQ(rasters.size(), 2u);
    EXPECT_EQ(rasters[0].svg, svg.get());
    EXPECT_EQ(rasters[1].svg, svg.get());

    EXPECT_EQ(rasters[0].path_id, 0u);
    EXPECT_EQ(rasters[1].path_id, 1u);

    affine_transform_t expected1 = {
      .sx  = 0.73209861711467406,
      .shx = 0.68119866031781207,
      .shy = -0.68119866031781207,
      .sy  = 0.73209861711467406,
      .tx  = 103.41615295410156,
      .ty  = 388.8668212890625,
    };

    affine_transform_t expected2 = {
      .sx  = 0.92927454033060863,
      .shx = 0.36938980588713594,
      .shy = -0.36938980588713594,
      .sy  = 0.92927454033060863,
      .tx  = -230.42375183105469,
      .ty  = 110.71320343017578,
    };
    EXPECT_AFFINE_TRANSFORM_EQ(rasters[0].transform, expected1);
    EXPECT_AFFINE_TRANSFORM_EQ(rasters[1].transform, expected2);
  }
}
