// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_image.h"

#include <gtest/gtest.h>

#include "tests/mock_spinel/mock_spinel_test_utils.h"

class SpinelImageTest : public mock_spinel::Test {
};

TEST_F(SpinelImageTest, SimpleTest)
{
  SpinelImage image;
  image.init(context_);

  ASSERT_EQ(image.context, context_);

  // Simply verify that all handles are defined and different from the default
  // ones provided by mock_spinel::Test.
  ASSERT_TRUE(image.path_builder);
  ASSERT_NE(image.path_builder, path_builder_);

  ASSERT_TRUE(image.raster_builder);
  ASSERT_NE(image.raster_builder, raster_builder_);

  ASSERT_TRUE(image.composition);
  ASSERT_NE(image.composition, composition_);

  ASSERT_TRUE(image.styling);
  ASSERT_NE(image.styling, styling_);

  image.reset();

  ASSERT_FALSE(image.path_builder);
  ASSERT_FALSE(image.raster_builder);
  ASSERT_FALSE(image.composition);
  ASSERT_FALSE(image.styling);

  ASSERT_FALSE(image.context);
}
