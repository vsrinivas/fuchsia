// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/atomic_optional.h"

#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(AtomicOptionalTest, SwapPop) {
  AtomicOptional<int> v;

  EXPECT_EQ(v.pop(), std::nullopt);
  EXPECT_EQ(v.swap(1), std::nullopt);
  EXPECT_EQ(v.swap(2), 1);
  EXPECT_EQ(v.pop(), 2);
  EXPECT_EQ(v.pop(), std::nullopt);
}

TEST(AtomicOptionalTest, SetMustBeEmpty) {
  AtomicOptional<int> v;

  v.set_must_be_empty(1);
  EXPECT_EQ(v.pop(), 1);
}

}  // namespace
}  // namespace media_audio
