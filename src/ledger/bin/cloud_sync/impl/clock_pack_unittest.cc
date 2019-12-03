// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/clock_pack.h"

#include "gmock/gmock.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_sync {
namespace {

using ClockPackTest = ::testing::TestWithParam<storage::Clock>;

TEST_P(ClockPackTest, EncodeDecode) {
  const storage::Clock entry = GetParam();
  cloud_provider::ClockPack pack = EncodeClock(entry);
  storage::Clock output;
  ASSERT_TRUE(DecodeClock(std::move(pack), &output));
  EXPECT_EQ(entry, output);
}

INSTANTIATE_TEST_SUITE_P(
    ClockPackTest, ClockPackTest,
    ::testing::Values(storage::Clock{},
                      storage::Clock{{clocks::DeviceId{"device_0", 1}, storage::ClockTombstone{}},
                                     {clocks::DeviceId{"device_1", 1},
                                      storage::DeviceEntry{storage::ClockEntry{"commit1", 1},
                                                           storage::ClockEntry{"commit1", 1}}},
                                     {clocks::DeviceId{"device_2", 4},
                                      storage::DeviceEntry{storage::ClockEntry{"commit4", 4},
                                                           storage::ClockEntry{"commit4", 4}}},
                                     {clocks::DeviceId{"device_3", 1}, storage::ClockDeletion{}}}));

}  // namespace
}  // namespace cloud_sync
