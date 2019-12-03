// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace storage {
namespace {

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

class LedgerStorageTest : public ledger::TestWithEnvironment {
 public:
  LedgerStorageTest()
      : encryption_service_(dispatcher()),
        db_factory_(dispatcher()),
        storage_(&environment_, &encryption_service_, &db_factory_,
                 ledger::DetachedPath(tmpfs_.root_fd()), CommitPruningPolicy::NEVER,
                 &device_id_manager_) {}

  LedgerStorageTest(const LedgerStorageTest&) = delete;
  LedgerStorageTest& operator=(const LedgerStorageTest&) = delete;
  ~LedgerStorageTest() override = default;

  // ledger::TestWithEnvironment:
  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();

    ASSERT_EQ(storage_.Init(), Status::OK);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  encryption::FakeEncryptionService encryption_service_;

 protected:
  fake::FakeDbFactory db_factory_;
  clocks::DeviceIdManagerEmptyImpl device_id_manager_;
  LedgerStorageImpl storage_;
};

TEST_F(LedgerStorageTest, CreateGetCreatePageStorage) {
  PageId page_id = "1234";
  Status status;
  std::unique_ptr<PageStorage> page_storage;
  bool called;
  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
  EXPECT_EQ(page_storage, nullptr);

  storage_.CreatePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_storage->GetId(), page_id);

  page_storage.reset();
  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(nullptr, page_storage);
}

TEST_F(LedgerStorageTest, CreateDeletePageStorage) {
  PageId page_id = "1234";
  Status status;
  bool called;
  std::unique_ptr<PageStorage> page_storage;
  storage_.CreatePageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  ASSERT_NE(nullptr, page_storage);
  ASSERT_EQ(page_storage->GetId(), page_id);
  page_storage.reset();

  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(nullptr, page_storage);

  storage_.DeletePageStorage(page_id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  storage_.GetPageStorage(
      page_id, callback::Capture(callback::SetWhenCalled(&called), &status, &page_storage));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
  EXPECT_EQ(page_storage, nullptr);
}

TEST_F(LedgerStorageTest, DeletePageStorageNotFound) {
  PageId page_id = "1234";
  Status status;
  bool called;

  storage_.DeletePageStorage(page_id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerStorageTest, ListNoPages) {
  Status status;
  bool called;

  std::set<PageId> listed_page_ids;
  storage_.ListPages(
      callback::Capture(callback::SetWhenCalled(&called), &status, &listed_page_ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(listed_page_ids, IsEmpty());
}

TEST_F(LedgerStorageTest, ListPages) {
  std::vector<PageId> all_page_ids({"1234", "5678", "90AB"});
  Status status;
  bool called;

  // The pages storages are listed after they are created...
  std::vector<std::unique_ptr<PageStorage>> page_storages(all_page_ids.size());
  std::vector<PageId> expected_page_ids;
  for (size_t i = 0; i < all_page_ids.size(); i++) {
    storage_.CreatePageStorage(all_page_ids[i], callback::Capture(callback::SetWhenCalled(&called),
                                                                  &status, &page_storages[i]));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, page_storages[i]);

    expected_page_ids.push_back(all_page_ids[i]);
    std::set<PageId> listed_page_ids;
    storage_.ListPages(
        callback::Capture(callback::SetWhenCalled(&called), &status, &listed_page_ids));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    ASSERT_THAT(listed_page_ids, ElementsAreArray(expected_page_ids));
  }

  // ... destroying the |PageStorage| pointers that were returned on creation
  // does not cause the page storages to be "lost" and not listed...
  for (size_t i = 0; i < all_page_ids.size(); i++) {
    page_storages[i].reset();
    std::set<PageId> listed_page_ids;
    storage_.ListPages(
        callback::Capture(callback::SetWhenCalled(&called), &status, &listed_page_ids));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    ASSERT_THAT(listed_page_ids, ElementsAreArray(all_page_ids));
  }

  // ... deleting the page storages does cause them to no longer be listed.
  for (const auto& page_id : all_page_ids) {
    storage_.DeletePageStorage(page_id,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    expected_page_ids.erase(expected_page_ids.begin());

    std::set<PageId> listed_page_ids;
    storage_.ListPages(
        callback::Capture(callback::SetWhenCalled(&called), &status, &listed_page_ids));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    ASSERT_THAT(listed_page_ids, ElementsAreArray(expected_page_ids));
  }
}

}  // namespace
}  // namespace storage
