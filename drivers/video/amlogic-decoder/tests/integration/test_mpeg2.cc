// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "tests/test_support.h"

#include "bear.mpeg2.h"

class TestMpeg2 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->EnableVideoPower();
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer());

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    EXPECT_EQ(ZX_OK, video->ParseVideo(bear_mpeg2, bear_mpeg2_len));

    video.reset();
  }
};
TEST(MPEG2, Decode) { TestMpeg2::Decode(); }
