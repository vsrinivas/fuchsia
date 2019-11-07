// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../image.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>
#include <zircon/pixelformat.h>

#include <zxtest/zxtest.h>

#include "../controller.h"
#include "../fence.h"
#include "base.h"

namespace display {

class ImageTest : public TestBase {};

TEST_F(ImageTest, MultipleAcquiresAllowed) {
  async::TestLoop loop;
  zx::vmo vmo, dup_vmo;
  ASSERT_OK(zx::vmo::create(1024 * 600 * 4, 0u, &vmo));
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo));
  image_t info = {};
  info.width = 1024;
  info.height = 600;
  info.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  ASSERT_OK(controller()->dc()->ImportVmoImage(&info, std::move(dup_vmo), /*offset=*/0u));
  Image image(controller(), info, std::move(vmo), /*stride=*/0);

  EXPECT_TRUE(image.Acquire());
  image.DiscardAcquire();
  EXPECT_TRUE(image.Acquire());
  image.EarlyRetire();
  loop.RunUntilIdle();
}

}  // namespace display
