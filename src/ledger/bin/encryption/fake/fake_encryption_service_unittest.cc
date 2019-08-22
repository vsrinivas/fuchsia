// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"

#include <string>

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/types.h"

namespace encryption {
namespace {

TEST(FakeEncryptionServiceTest, GetEntryIdMergeCommit) {
  // We don't need an async_dispatcher here as we're only using the synchronous methods.
  auto fake_encryption_service = FakeEncryptionService(nullptr);

  storage::CommitId parent_id1 = "commit1";
  storage::CommitId parent_id2 = "commit2";
  std::string entry_name = "Name";
  std::string operation_list = "AADD";

  std::string entry_id = fake_encryption_service.GetEntryIdForMerge(entry_name, parent_id1,
                                                                    parent_id2, operation_list);
  // For merge commits, calling this method with the same parameters must result in the same entry
  // id.
  std::string entry_id0 = fake_encryption_service.GetEntryIdForMerge(entry_name, parent_id1,
                                                                     parent_id2, operation_list);
  EXPECT_EQ(entry_id, entry_id0);

  // Changing any of the parameters must result in different entry id.
  EXPECT_NE(entry_id,
            fake_encryption_service.GetEntryIdForMerge(entry_name, parent_id1, parent_id2, "AD"));
  EXPECT_NE(entry_id, fake_encryption_service.GetEntryIdForMerge(entry_name, parent_id1, "commit3",
                                                                 operation_list));
  EXPECT_NE(entry_id, fake_encryption_service.GetEntryIdForMerge("Surname", parent_id1, parent_id2,
                                                                 operation_list));
}

TEST(FakeEncryptionServiceTest, GetEntryIdNonMergeCommit) {
  auto fake_encryption_service = FakeEncryptionService(nullptr);
  EXPECT_NE(fake_encryption_service.GetEntryId(), fake_encryption_service.GetEntryId());
}

}  // namespace
}  // namespace encryption
