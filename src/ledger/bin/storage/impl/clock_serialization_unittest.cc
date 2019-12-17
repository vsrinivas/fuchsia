// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/clock_serialization.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/rng/random.h"

namespace storage {
namespace {

using ::testing::AllOf;
using ::testing::Field;

using ClockSerializationTest = ::ledger::TestWithEnvironment;

clocks::DeviceId RandomDeviceId(ledger::Random* random) {
  std::string device_fingerprint;
  device_fingerprint.resize(16);
  random->Draw(&device_fingerprint);
  return clocks::DeviceId{convert::ToHex(device_fingerprint), random->Draw<uint64_t>()};
}

ClockEntry RandomClockEntry(ledger::Random* random) {
  return ClockEntry{RandomCommitId(random), random->Draw<uint64_t>()};
}

TEST_F(ClockSerializationTest, SerializeDeserializeClock) {
  Clock clock;
  // Add a few head entries, cloud entries, and tombstones.
  clock.emplace(
      RandomDeviceId(environment_.random()),
      DeviceEntry{RandomClockEntry(environment_.random()),
                  std::make_optional<ClockEntry>(RandomClockEntry(environment_.random()))});
  clock.emplace(RandomDeviceId(environment_.random()),
                DeviceEntry{RandomClockEntry(environment_.random()), std::nullopt});

  clock.emplace(RandomDeviceId(environment_.random()), ClockTombstone());

  std::string data = SerializeClock(clock);

  Clock actual_clock;
  EXPECT_TRUE(ExtractClockFromStorage(std::move(data), &actual_clock));
  EXPECT_EQ(actual_clock, clock);
}

TEST_F(ClockSerializationTest, SerializeDeserializeDeviceId) {
  clocks::DeviceId id = RandomDeviceId(environment_.random());

  std::string data = SerializeDeviceId(id);

  clocks::DeviceId actual_id;
  EXPECT_TRUE(ExtractDeviceIdFromStorage(std::move(data), &actual_id));
  EXPECT_EQ(actual_id, id);
}

}  // namespace
}  // namespace storage
