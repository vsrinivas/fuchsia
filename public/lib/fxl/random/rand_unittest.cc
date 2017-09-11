// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/rand.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(Random, RandUint64) {
  for (int i = 0; i < 256; ++i) {
    uint64_t num = RandUint64();
    (void)num;
  }
}

TEST(Random, RandBytes) {
  size_t sizes[] = {1, 16, 256, 4096};

  for (auto size : sizes) {
    std::vector<uint8_t> buf;
    buf.resize(size);
    EXPECT_TRUE(RandBytes(buf.data(), buf.size()));
  }
}

}  // namespace
}  // namespace fxl
