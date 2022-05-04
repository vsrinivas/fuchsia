// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/packet_timestamp_units.h"

#include <gtest/gtest.h>

namespace fmlib {
namespace {

// Tests the |Create| method.
TEST(PacketTimestampUnits, Create) {
  constexpr int64_t kPacketTimestampInterval = 1234;
  constexpr zx::duration kPresentationInterval = zx::duration(4321);

  // Passing null produces a null unique_ptr.
  auto result = PacketTimestampUnits::Create(nullptr);
  EXPECT_FALSE(result);

  fuchsia::media2::PacketTimestampUnits fidl{.packet_timestamp_interval = kPacketTimestampInterval,
                                             .presentation_interval = kPresentationInterval.get()};
  result = PacketTimestampUnits::Create(&fidl);
  EXPECT_TRUE(result);
  EXPECT_EQ(kPacketTimestampInterval, result->packet_timestamp_interval());
  EXPECT_EQ(kPresentationInterval, result->presentation_interval());
}

// Tests the |IsValid| method.
TEST(PacketTimestampUnits, IsValid) {
  {
    PacketTimestampUnits under_test;
    EXPECT_FALSE(under_test.is_valid());
    EXPECT_FALSE(under_test);
  }

  {
    PacketTimestampUnits under_test(0, zx::duration(0));
    EXPECT_FALSE(under_test.is_valid());
    EXPECT_FALSE(under_test);
  }

  {
    PacketTimestampUnits under_test(1, zx::duration(1));
    EXPECT_TRUE(under_test.is_valid());
    EXPECT_TRUE(under_test);
  }
}

// Tests the |fidl| and |fidl_ptr| methods.
TEST(PacketTimestampUnits, Fidl) {
  constexpr int64_t kPacketTimestampInterval = 1234;
  constexpr zx::duration kPresentationInterval = zx::duration(4321);

  {
    PacketTimestampUnits under_test;
    auto result = under_test.fidl();
    EXPECT_EQ(0, result.packet_timestamp_interval);
    EXPECT_EQ(0, result.presentation_interval);
  }

  {
    PacketTimestampUnits under_test(kPacketTimestampInterval, kPresentationInterval);
    auto result = under_test.fidl();
    EXPECT_EQ(kPacketTimestampInterval, result.packet_timestamp_interval);
    EXPECT_EQ(kPresentationInterval.get(), result.presentation_interval);
  }

  {
    PacketTimestampUnits under_test;
    auto result = under_test.fidl_ptr();
    EXPECT_FALSE(result);
  }

  {
    PacketTimestampUnits under_test(kPacketTimestampInterval, kPresentationInterval);
    auto result = under_test.fidl_ptr();
    EXPECT_TRUE(result);
    EXPECT_EQ(kPacketTimestampInterval, result->packet_timestamp_interval);
    EXPECT_EQ(kPresentationInterval.get(), result->presentation_interval);
  }
}

// Tests the |ToTimestamp| method.
TEST(PacketTimestampUnits, ToTimestamp) {
  constexpr int64_t kPacketTimestampInterval = 1234;
  constexpr zx::duration kPresentationInterval = zx::duration(4321);

  PacketTimestampUnits under_test(kPacketTimestampInterval, kPresentationInterval);
  EXPECT_EQ(0, under_test.ToTimestamp(zx::duration()));
  EXPECT_EQ(kPacketTimestampInterval, under_test.ToTimestamp(kPresentationInterval));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
}

// Tests the |ToPresentationTime| method.
TEST(PacketTimestampUnits, ToPresentationTime) {
  constexpr int64_t kPacketTimestampInterval = 1234;
  constexpr zx::duration kPresentationInterval = zx::duration(4321);

  PacketTimestampUnits under_test(kPacketTimestampInterval, kPresentationInterval);
  EXPECT_EQ(zx::duration(), under_test.ToPresentationTime(0));
  EXPECT_EQ(kPresentationInterval, under_test.ToPresentationTime(kPacketTimestampInterval));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
}

}  // namespace
}  // namespace fmlib
