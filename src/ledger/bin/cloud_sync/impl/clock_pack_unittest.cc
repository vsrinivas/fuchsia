// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/clock_pack.h"

#include "gmock/gmock.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/run_in_coroutine.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace cloud_sync {
namespace {

using ClockPackTest = ledger::TestWithEnvironment;

TEST_F(ClockPackTest, EncodeDecode) {
  TestPageStorage storage(dispatcher());
  encryption::FakeEncryptionService encryption_service(dispatcher());
  storage.remote_id_to_commit_id[encryption_service.EncodeCommitId("commit1")] = "commit1";
  storage.remote_id_to_commit_id[encryption_service.EncodeCommitId("commit4")] = "commit4";

  const storage::Clock entry{
      {clocks::DeviceId{"device_0", 1}, storage::ClockTombstone{}},
      {clocks::DeviceId{"device_1", 1},
       storage::DeviceEntry{storage::ClockEntry{"commit1", 1}, storage::ClockEntry{"commit1", 1}}},
      {clocks::DeviceId{"device_2", 4},
       storage::DeviceEntry{storage::ClockEntry{"commit4", 4}, storage::ClockEntry{"commit4", 4}}},
      {clocks::DeviceId{"device_3", 1}, storage::ClockDeletion{}}};
  cloud_provider::ClockPack pack = EncodeClock(&encryption_service, entry);
  ledger::Status status;
  storage::Clock output;
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    status = DecodeClock(handler, &storage, std::move(pack), &output);
  }));
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(entry, output);
}

}  // namespace
}  // namespace cloud_sync
