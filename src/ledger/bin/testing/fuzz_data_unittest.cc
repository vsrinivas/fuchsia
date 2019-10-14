// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/fuzz_data.h"

#include "gtest/gtest.h"
#include "src/lib/fxl/arraysize.h"

namespace ledger {
namespace {

TEST(FuzzDataTest, ShortInt) {
  uint8_t v = 3;
  FuzzData data(&v, sizeof(v));
  EXPECT_EQ(data.GetNextSmallInt(), v);
  EXPECT_EQ(data.GetNextSmallInt(), std::nullopt);
}

TEST(FuzzDataTest, RemainingString) {
  uint8_t v[] = {3, 'h', 'e', 'l', 'l', 'o'};
  FuzzData data(v, sizeof(v[0]) * arraysize(v));
  EXPECT_EQ(data.GetNextSmallInt(), v[0]);
  EXPECT_EQ(data.RemainingString(), "hello");
  EXPECT_EQ(data.RemainingString(), "");
  EXPECT_EQ(data.GetNextSmallInt(), std::nullopt);
}

}  // namespace
}  // namespace ledger
