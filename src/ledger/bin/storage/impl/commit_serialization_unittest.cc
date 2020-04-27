// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_serialization.h"

#include <gtest/gtest.h>

#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using CommitSerializationTest = ::ledger::TestWithEnvironment;

TEST_F(CommitSerializationTest, SerializeDeserialize) {
  CommitId id = RandomCommitId(environment_.random());

  const IdStorage* fb_id_storage = ToIdStorage(id);
  CommitIdView actual_id = ToCommitIdView(fb_id_storage);

  EXPECT_EQ(actual_id, id);
}

}  // namespace
}  // namespace storage
