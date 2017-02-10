// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/common/manufacturer_names.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace common {
namespace {

TEST(ManufacturerNamesTest, ExhaustiveLookUp) {
  constexpr uint16_t kMaxManufacturerId = 0x048D;

  // Looking up beyond the maximum entry shouldn't crash.
  EXPECT_EQ("(unknown)", GetManufacturerName(kMaxManufacturerId));

  // Do an incremental look up of all entries up to |kMaxManufacturerId|. The
  // code shouldn't crash. We don't make an exact comparison of all entries
  // since this is a resiliency test.
  for (uint16_t i = 0; i < kMaxManufacturerId; ++i) {
    std::string result = GetManufacturerName(i);
    EXPECT_FALSE(result.empty());
    EXPECT_NE("(unknown)", result);
  }
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
