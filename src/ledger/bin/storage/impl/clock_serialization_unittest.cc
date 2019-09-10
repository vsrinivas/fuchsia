// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/clock_serialization.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

using ClockSerializationTest = ::ledger::TestWithEnvironment;

TEST_F(ClockSerializationTest, SerializeDeserialize) {
  ClockEntry entry{RandomCommitId(environment_.random()), 12};
  std::string data;
  SerializeClockEntry(entry, &data);

  std::string device_id = "device_id";
  std::vector<std::pair<std::string, std::string>> db_entries;
  db_entries.emplace_back(device_id, data);

  std::map<DeviceId, ClockEntry> actual_entries;
  EXPECT_TRUE(ExtractClockFromStorage(std::move(db_entries), &actual_entries));
  EXPECT_THAT(actual_entries, ElementsAre(Pair(device_id, entry)));
}

}  // namespace
}  // namespace storage
