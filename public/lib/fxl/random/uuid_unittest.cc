// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/uuid.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(Random, Uuid) {
  for (int i = 0; i < 256; ++i) {
    auto uuid = GenerateUUID();
    EXPECT_TRUE(IsValidUUID(uuid));
    EXPECT_TRUE(IsValidUUIDOutputString(uuid));
  }
}

}  // namespace
}  // namespace fxl
