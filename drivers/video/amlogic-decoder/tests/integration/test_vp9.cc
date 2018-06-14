// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "hevcdec.h"
#include "tests/test_support.h"

class TestVP9 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(true));

    video->InitializeInterrupts();

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    video.reset();
  }
};
TEST(VP9, Decode) { TestVP9::Decode(); }
