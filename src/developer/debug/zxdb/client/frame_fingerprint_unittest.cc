// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_fingerprint.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(FrameFingerprint, Newer) {
  // Larger stack addresses mean older frames.
  FrameFingerprint newer_physical(0x1000, 1);
  FrameFingerprint older_physical(0x2000, 2);
  EXPECT_TRUE(FrameFingerprint::Newer(newer_physical, older_physical));
  EXPECT_FALSE(FrameFingerprint::Newer(older_physical, newer_physical));

  // Identical stack pointers should check the inline counts. Higher counts are newer.
  FrameFingerprint newer_inline(0x1000, 2);
  FrameFingerprint older_inline(0x1000, 1);
  EXPECT_TRUE(FrameFingerprint::Newer(newer_inline, older_inline));
  EXPECT_FALSE(FrameFingerprint::Newer(older_inline, newer_inline));
}

}  // namespace zxdb
