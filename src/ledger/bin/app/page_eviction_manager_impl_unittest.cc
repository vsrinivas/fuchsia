// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_manager_impl.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
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
    callback(page_closed_and_synced_status, closed_and_synced);
  }

  void PageIsClosedOfflineAndEmpty(
      absl::string_view ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override {
    callback(Status::OK, closed_and_empty);
  }

  void DeletePageStorage(absl::string_view /*ledger_name*/, storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override {
    deleted_pages.push_back(convert::ToString(page_id));
    callback(Status::OK);
  }

  std::vector<storage::PageId> deleted_pages;

  PagePredicateResult closed_and_synced = PagePredicateResult::YES;
  Status page_closed_and_synced_status = Status::OK;

  PagePredicateResult closed_and_empty = PagePredicateResult::YES;
};

class PageEvictionManagerTest : public TestWithEnvironment {
 public:
  PageEvictionManagerTest()
      : tmp_location_(environment_.file_system()->CreateScopedTmpLocation()),
        db_factory_(environment_.file_system(), environment_.dispatcher()) {}
  PageEvictionManagerTest(const PageEvictionManagerTest&) = delete;
  PageEvictionManagerTest& operator=(const PageEvictionManagerTest&) = delete;

  // TestWithEnvironment:
  void SetUp() override {
    ResetPageUsageDb();
    page_eviction_manager_ = std::make_unique<PageEvictionManagerImpl>(&environment_, db_.get());
    policy_ =
        NewLeastRecentyUsedPolicy(environment_.coroutine_service(), page_eviction_manager_.get());
    page_eviction_manager_->SetDelegate(&delegate_);
  }

  void ResetPageUsageDb() {
    EXPECT_TRUE(RunInCoroutine([this](coroutine::CoroutineHandler* handler) {
      std::unique_ptr<storage::Db> leveldb;
      Status status;
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
  std::unique_ptr<PageEvictionManagerImpl> page_eviction_manager_;
  std::unique_ptr<PageEvictionPolicy> policy_;
};

TEST_F(PageEvictionManagerTest, NoEvictionWithoutPages) {
  bool called;
  Status status;

  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, AtLeastOneEvictionWhenPossible) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page1);
  page_eviction_manager_->MarkPageClosed(ledger_name, page1);
  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(delegate_.deleted_pages.empty());
}

TEST_F(PageEvictionManagerTest, DontEvictUnsyncedNotEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  delegate_.closed_and_synced = PagePredicateResult::NO;
  delegate_.closed_and_empty = PagePredicateResult::NO;

  page_eviction_manager_->MarkPageOpened(ledger_name, page1);
  page_eviction_manager_->MarkPageClosed(ledger_name, page1);
  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, DontEvictOpenPages) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // Close the page. It can now be evicted.
  page_eviction_manager_->MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(PageEvictionManagerTest, DontEvictAnEvictedPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page);
  RunLoopUntilIdle();
  page_eviction_manager_->MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));

  delegate_.deleted_pages.clear();
  // Try to clean up again. We shouldn't be able to evict any pages.
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, PageNotFoundIsNotAnError) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page);
  page_eviction_manager_->MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  delegate_.page_closed_and_synced_status = Status::PAGE_NOT_FOUND;

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, EvictUnsyncedButEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  delegate_.closed_and_synced = PagePredicateResult::NO;
  delegate_.closed_and_empty = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page1);
  page_eviction_manager_->MarkPageClosed(ledger_name, page1);
  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page1));
}

TEST_F(PageEvictionManagerTest, EvictSyncedAndNotEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  delegate_.closed_and_synced = PagePredicateResult::YES;
  delegate_.closed_and_empty = PagePredicateResult::NO;

  page_eviction_manager_->MarkPageOpened(ledger_name, page1);
  page_eviction_manager_->MarkPageClosed(ledger_name, page1);
  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page1));
}

TEST_F(PageEvictionManagerTest, DontEvictIfPageWasOpenedDuringQuery) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  // Page is offline and synced, but PageIsClosedOfflineAndEmpty returned
  // |PAGE_OPENED|, meaning it was opened during the operation. The page cannot
  // be evicted.
  delegate_.closed_and_synced = PagePredicateResult::YES;
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;

  page_eviction_manager_->MarkPageOpened(ledger_name, page1);
  page_eviction_manager_->MarkPageClosed(ledger_name, page1);
  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
  delegate_.deleted_pages.clear();

  // Page is offline and empty, but PageIsClosedAndSynced returned
  // |PAGE_OPENED|. The page cannot be evicted.
  delegate_.closed_and_synced = PagePredicateResult::PAGE_OPENED;
  delegate_.closed_and_empty = PagePredicateResult::YES;

  page_eviction_manager_->MarkPageOpened(ledger_name, page2);
  page_eviction_manager_->MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, IsEmpty) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  bool on_discardable_called = false;

  page_eviction_manager_->SetOnDiscardable([&] { on_discardable_called = true; });

  EXPECT_TRUE(page_eviction_manager_->IsDiscardable());
  EXPECT_FALSE(on_discardable_called);

  // PageEvictionManagerImpl should be empty if there is no pending operation on: MarkPageOpened,
  // MarkPageClosed, or TryEvictPages.
  on_discardable_called = false;
  page_eviction_manager_->MarkPageOpened(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_->IsDiscardable());
  EXPECT_FALSE(on_discardable_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_->IsDiscardable());
  EXPECT_TRUE(on_discardable_called);

  on_discardable_called = false;
  page_eviction_manager_->MarkPageClosed(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_->IsDiscardable());
  EXPECT_FALSE(on_discardable_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_->IsDiscardable());
  EXPECT_TRUE(on_discardable_called);

  bool called;
  Status status;
  on_discardable_called = false;
  page_eviction_manager_->TryEvictPages(policy_.get(), Capture(SetWhenCalled(&called), &status));
  EXPECT_FALSE(page_eviction_manager_->IsDiscardable());
  EXPECT_FALSE(on_discardable_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(page_eviction_manager_->IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(PageEvictionManagerTest, TryEvictPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty and
  // from PageIsClosedAndSynced is |NO|.
  delegate_.closed_and_empty = PagePredicateResult::NO;
  delegate_.closed_and_synced = PagePredicateResult::NO;

  bool called;
  Status status;
  PageWasEvicted was_evicted;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;
  delegate_.closed_and_synced = PagePredicateResult::YES;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedAndSynced is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  delegate_.closed_and_synced = PagePredicateResult::PAGE_OPENED;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is evicted if the result from PageIsClosedOfflineAndEmpty is
  // |YES|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  delegate_.closed_and_synced = PagePredicateResult::NO;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));

  // The page is evicted if the result from PageIsClosedAndSynced is |YES|.
  delegate_.deleted_pages.clear();
  delegate_.closed_and_empty = PagePredicateResult::NO;
  delegate_.closed_and_synced = PagePredicateResult::YES;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(PageEvictionManagerTest, EvictEmptyPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |NO|.
  delegate_.closed_and_empty = PagePredicateResult::NO;

  bool called;
  Status status;
  PageWasEvicted was_evicted;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_EMPTY,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_EMPTY,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is evicted if the result from PageIsClosedOfflineAndEmpty is
  // |YES|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  page_eviction_manager_->TryEvictPage(ledger_name, page, PageEvictionCondition::IF_EMPTY,
                                       Capture(SetWhenCalled(&called), &status, &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

}  // namespace
}  // namespace ledger
