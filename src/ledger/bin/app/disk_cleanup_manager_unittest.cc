// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class FakeDelegate : public PageEvictionManager::Delegate {
 public:
  void PageIsClosedAndSynced(
      fxl::StringView /*ledger_name*/, storage::PageIdView /*page_id*/,
      fit::function<void(storage::Status, PagePredicateResult)> callback)
      override {
    callback(storage::Status::OK, PagePredicateResult::YES);
  }

  void PageIsClosedOfflineAndEmpty(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(storage::Status, PagePredicateResult)> callback)
      override {
    callback(storage::Status::OK, closed_offline_empty);
  }

  void DeletePageStorage(
      fxl::StringView /*ledger_name*/, storage::PageIdView page_id,
      fit::function<void(storage::Status)> callback) override {
    deleted_pages.push_back(page_id.ToString());
    callback(storage::Status::OK);
  }

  std::vector<storage::PageId> deleted_pages;

  PagePredicateResult closed_offline_empty = PagePredicateResult::YES;
};

class DiskCleanupManagerTest : public TestWithEnvironment {
 public:
  DiskCleanupManagerTest()
      : db_factory_(environment_.dispatcher()),
        disk_cleanup_manager_(&environment_, &db_factory_,
                              DetachedPath(tmpfs_.root_fd())) {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    disk_cleanup_manager_.Init();
    RunLoopUntilIdle();
    disk_cleanup_manager_.SetPageEvictionDelegate(&delegate_);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  storage::fake::FakeDbFactory db_factory_;

 protected:
  FakeDelegate delegate_;
  DiskCleanupManagerImpl disk_cleanup_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiskCleanupManagerTest);
};

TEST_F(DiskCleanupManagerTest, DontEvictNonEmptyPagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page cannot be evicted if its not empty and offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::NO;
  disk_cleanup_manager_.OnPageUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, DontEvictUnknownEmptyPagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page cannot be evicted if we can't determine whether it is empty and
  // offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::PAGE_OPENED;
  disk_cleanup_manager_.OnPageUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, EvictEmptyOfflinePagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page should be evicted is it is empty and offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_.OnPageUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(DiskCleanupManagerTest, DontEvictPagesOnPageClosed) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

}  // namespace
}  // namespace ledger
