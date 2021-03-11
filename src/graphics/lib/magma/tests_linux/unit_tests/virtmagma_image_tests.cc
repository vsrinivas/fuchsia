// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magma.h>

#include <gtest/gtest.h>

#include "drm_fourcc.h"

class MagmaImageTest : public ::testing::Test {
 public:
  magma_connection_t connection_;
};

TEST_F(MagmaImageTest, Placeholder) {
  magma_image_create_info_t create_info = {};
  magma_image_info_t image_info = {};
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_UNIMPLEMENTED, magma_virt_create_image(connection_, &create_info, &image));
  EXPECT_EQ(MAGMA_STATUS_UNIMPLEMENTED,
            magma_virt_get_image_params(connection_, image, &image_info));
}
