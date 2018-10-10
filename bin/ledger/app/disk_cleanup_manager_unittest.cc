// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/disk_cleanup_manager_impl.h"

#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class FakeDelegate : public PageEvictionManager::Delegate {
 public:
  void PageIsClosedAndSynced(
      fxl::StringView /*ledger_name*/, storage::PageIdView /*page_id*/,
      fit::function<void(Status, PagePredicateResult)> callback) override {
    callback(Status::OK, PagePredicateResult::YES);
  }

  void PageIsClosedOfflineAndEmpty(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override {
    callback(Status::OK, closed_offline_empty);
  }

  void DeletePageStorage(fxl::StringView /*ledger_name*/,
                         storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override {
    deleted_pages.push_back(page_id.ToString());
    callback(Status::OK);
  }

  std::vector<storage::PageId> deleted_pages;

  PagePredicateResult closed_offline_empty = PagePredicateResult::YES;
};

class DiskCleanupManagerTest : public TestWithEnvironment {
 public:
  DiskCleanupManagerTest()
      : disk_cleanup_manager_(&environment_, DetachedPath(tmpfs_.root_fd())) {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    EXPECT_EQ(Status::OK, disk_cleanup_manager_.Init());
    RunLoopUntilIdle();
    disk_cleanup_manager_.SetPageEvictionDelegate(&delegate_);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  coroutine::CoroutineServiceImpl coroutine_service_;

 protected:
  FakeDelegate delegate_;
  DiskCleanupManagerImpl disk_cleanup_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiskCleanupManagerTest);
};

TEST_F(DiskCleanupManagerTest, DontEvictNonEmptyPagesOnPageClosed) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page cannot be evicted if its not empty and offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::NO;
  disk_cleanup_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, DontEvictUnknownEmptyPagesOnPageClosed) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page cannot be evicted if we can't determine whether it is empty and
  // offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::PAGE_OPENED;
  disk_cleanup_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, EvictEmptyOfflinePagesOnPageClosed) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page should be evicted is it is empty and offline.
  disk_cleanup_manager_.OnPageOpened(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

}  // namespace
}  // namespace ledger
