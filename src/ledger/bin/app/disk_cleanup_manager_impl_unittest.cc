// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"

#include <lib/gtest/test_loop_fixture.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class FakeDelegate : public PageEvictionManager::Delegate {
 public:
  void PageIsClosedAndSynced(absl::string_view /*ledger_name*/, storage::PageIdView /*page_id*/,
                             fit::function<void(Status, PagePredicateResult)> callback) override {
    callback(Status::OK, PagePredicateResult::YES);
  }

  void PageIsClosedOfflineAndEmpty(
      absl::string_view ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override {
    callback(Status::OK, closed_offline_empty);
  }

  void DeletePageStorage(absl::string_view /*ledger_name*/, storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override {
    deleted_pages.push_back(convert::ToString(page_id));
    callback(Status::OK);
  }

  std::vector<storage::PageId> deleted_pages;

  PagePredicateResult closed_offline_empty = PagePredicateResult::YES;
};

class DiskCleanupManagerTest : public TestWithEnvironment {
 public:
  DiskCleanupManagerTest()
      : tmp_location_(environment_.file_system()->CreateScopedTmpLocation()),
        db_factory_(environment_.file_system(), environment_.dispatcher()) {}

  DiskCleanupManagerTest(const DiskCleanupManagerTest&) = delete;
  DiskCleanupManagerTest& operator=(const DiskCleanupManagerTest&) = delete;

  // gtest::TestLoopFixture:
  void SetUp() override {
    ResetPageUsageDb();
    disk_cleanup_manager_ = std::make_unique<DiskCleanupManagerImpl>(&environment_, db_.get());
    disk_cleanup_manager_->SetPageEvictionDelegate(&delegate_);
  }

  void ResetPageUsageDb() {
    EXPECT_TRUE(RunInCoroutine([this](coroutine::CoroutineHandler* handler) {
      Status status;
      std::unique_ptr<storage::Db> leveldb;
      if (coroutine::SyncCall(
              handler,
              [this](fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) mutable {
                db_factory_.GetOrCreateDb(tmp_location_->path(),
                                          storage::DbFactory::OnDbNotFound::CREATE,
                                          std::move(callback));
              },
              &status, &leveldb) == coroutine::ContinuationStatus::INTERRUPTED) {
        // This should not be reached.
        FAIL();
        return;
      }
      dbview_factory_ = std::make_unique<DbViewFactory>(std::move(leveldb));
      db_ = std::make_unique<PageUsageDb>(
          &environment_, dbview_factory_->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));
    }));
  }

 private:
  std::unique_ptr<ScopedTmpLocation> tmp_location_;
  storage::fake::FakeDbFactory db_factory_;
  std::unique_ptr<DbViewFactory> dbview_factory_;
  std::unique_ptr<PageUsageDb> db_;

 protected:
  FakeDelegate delegate_;
  std::unique_ptr<DiskCleanupManagerImpl> disk_cleanup_manager_;
};

TEST_F(DiskCleanupManagerTest, DontEvictNonEmptyPagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  // The page cannot be evicted if its not empty and offline.
  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::NO;
  disk_cleanup_manager_->OnExternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, DontEvictUnknownEmptyPagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  // The page cannot be evicted if we can't determine whether it is empty and
  // offline.
  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::PAGE_OPENED;
  disk_cleanup_manager_->OnExternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, EvictEmptyOfflinePagesOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  // The page should be evicted is it is empty and offline.
  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_->OnExternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(DiskCleanupManagerTest, DontEvictPageWhenInternalConnectionsRemain) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnInternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnInternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnInternallyUnused(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_->OnExternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, DontEvictPageWhenExternalConnectionsRemain) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  disk_cleanup_manager_->OnInternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnExternallyUsed(ledger_name, page);
  disk_cleanup_manager_->OnExternallyUnused(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_->OnInternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(DiskCleanupManagerTest, DontEvictPageWhenNoExternalConnectionHappened) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  disk_cleanup_manager_->OnInternallyUsed(ledger_name, page);
  delegate_.closed_offline_empty = PagePredicateResult::YES;
  disk_cleanup_manager_->OnInternallyUnused(ledger_name, page);
  RunLoopUntilIdle();
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

}  // namespace
}  // namespace ledger
