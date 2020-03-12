// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/spinel/spinel_path_sink.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tests/common/spinel/spinel_test_utils.h"
#include "tests/mock_spinel/mock_spinel.h"
#include "tests/mock_spinel/mock_spinel_test_utils.h"

namespace {

class SpinelPathSinkTest : public mock_spinel::Test {
};

}  // namespace

TEST_F(SpinelPathSinkTest, SingleRect)
{
  SpinelPathSink sink(context_);

  EXPECT_TRUE(sink.addRectPath(1, 2, 3, 4));

  EXPECT_EQ(sink.size(), 1u);
  EXPECT_TRUE(sink.paths());
  EXPECT_SPN_PATH_VALID(sink.paths()[0]);

  const mock_spinel::Path * path = mock_context()->pathFor(sink.paths()[0]);

  static const float kExpectedPath[] = {
    MOCK_SPINEL_PATH_MOVE_TO_LITERAL(1, 2), MOCK_SPINEL_PATH_LINE_TO_LITERAL(4, 2),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(4, 6), MOCK_SPINEL_PATH_LINE_TO_LITERAL(1, 6),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(1, 2),
  };

  ASSERT_THAT(path->data, ::testing::ElementsAreArray(kExpectedPath));
}
