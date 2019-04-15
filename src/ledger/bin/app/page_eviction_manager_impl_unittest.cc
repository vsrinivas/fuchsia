// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_manager_impl.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
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
    callback(page_closed_and_synced_status, closed_and_synced);
  }

  void PageIsClosedOfflineAndEmpty(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(storage::Status, PagePredicateResult)> callback)
      override {
    callback(storage::Status::OK, closed_and_empty);
  }

  void DeletePageStorage(
      fxl::StringView /*ledger_name*/, storage::PageIdView page_id,
      fit::function<void(storage::Status)> callback) override {
    deleted_pages.push_back(page_id.ToString());
    callback(storage::Status::OK);
  }

  std::vector<storage::PageId> deleted_pages;

  PagePredicateResult closed_and_synced = PagePredicateResult::YES;
  storage::Status page_closed_and_synced_status = storage::Status::OK;

  PagePredicateResult closed_and_empty = PagePredicateResult::YES;
};

class PageEvictionManagerTest : public TestWithEnvironment {
 public:
  PageEvictionManagerTest()
      : db_factory_(environment_.dispatcher()),
        page_eviction_manager_(&environment_, &db_factory_,
                               DetachedPath(tmpfs_.root_fd())),
        policy_(NewLeastRecentyUsedPolicy(environment_.coroutine_service(),
                                          &page_eviction_manager_)) {}

  // TestWithEnvironment:
  void SetUp() override {
    page_eviction_manager_.Init();
    RunLoopUntilIdle();
    page_eviction_manager_.SetDelegate(&delegate_);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  storage::fake::FakeDbFactory db_factory_;

 protected:
  FakeDelegate delegate_;
  PageEvictionManagerImpl page_eviction_manager_;
  std::unique_ptr<PageEvictionPolicy> policy_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerTest);
};

TEST_F(PageEvictionManagerTest, NoEvictionWithoutPages) {
  bool called;
  storage::Status status;

  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, AtLeastOneEvictionWhenPossible) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::kPageIdSize, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::kPageIdSize, '2');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page1);
  page_eviction_manager_.MarkPageClosed(ledger_name, page1);
  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(delegate_.deleted_pages.empty());
}

TEST_F(PageEvictionManagerTest, DontEvictUnsyncedNotEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::kPageIdSize, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::kPageIdSize, '2');

  delegate_.closed_and_synced = PagePredicateResult::NO;
  delegate_.closed_and_empty = PagePredicateResult::NO;

  page_eviction_manager_.MarkPageOpened(ledger_name, page1);
  page_eviction_manager_.MarkPageClosed(ledger_name, page1);
  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, DontEvictOpenPages) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // Close the page. It can now be evicted.
  page_eviction_manager_.MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(PageEvictionManagerTest, DontEvictAnEvictedPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page);
  page_eviction_manager_.MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));

  delegate_.deleted_pages.clear();
  // Try to clean up again. We shouldn't be able to evict any pages.
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, PageNotFoundIsNotAnError) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  delegate_.closed_and_synced = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page);
  page_eviction_manager_.MarkPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  delegate_.page_closed_and_synced_status = storage::Status::PAGE_NOT_FOUND;

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, EvictUnsyncedButEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::kPageIdSize, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::kPageIdSize, '2');

  delegate_.closed_and_synced = PagePredicateResult::NO;
  delegate_.closed_and_empty = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page1);
  page_eviction_manager_.MarkPageClosed(ledger_name, page1);
  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page1));
}

TEST_F(PageEvictionManagerTest, EvictSyncedAndNotEmptyPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::kPageIdSize, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::kPageIdSize, '2');

  delegate_.closed_and_synced = PagePredicateResult::YES;
  delegate_.closed_and_empty = PagePredicateResult::NO;

  page_eviction_manager_.MarkPageOpened(ledger_name, page1);
  page_eviction_manager_.MarkPageClosed(ledger_name, page1);
  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page1));
}

TEST_F(PageEvictionManagerTest, DontEvictIfPageWasOpenedDuringQuery) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(::fuchsia::ledger::kPageIdSize, '1');
  storage::PageId page2 = std::string(::fuchsia::ledger::kPageIdSize, '2');

  // Page is offline and synced, but PageIsClosedOfflineAndEmpty returned
  // |PAGE_OPENED|, meaning it was opened during the operation. The page cannot
  // be evicted.
  delegate_.closed_and_synced = PagePredicateResult::YES;
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;

  page_eviction_manager_.MarkPageOpened(ledger_name, page1);
  page_eviction_manager_.MarkPageClosed(ledger_name, page1);
  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
  delegate_.deleted_pages.clear();

  // Page is offline and empty, but PageIsClosedAndSynced returned
  // |PAGE_OPENED|. The page cannot be evicted.
  delegate_.closed_and_synced = PagePredicateResult::PAGE_OPENED;
  delegate_.closed_and_empty = PagePredicateResult::YES;

  page_eviction_manager_.MarkPageOpened(ledger_name, page2);
  page_eviction_manager_.MarkPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());
}

TEST_F(PageEvictionManagerTest, IsEmpty) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');
  bool on_empty_called = false;

  page_eviction_manager_.set_on_empty([&] { on_empty_called = true; });

  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);

  // PageEvictionManagerImpl should be empty if there is no pending operation
  // on: OnPageOpened, OnPageClosed, or TryEvictPages.
  on_empty_called = false;
  page_eviction_manager_.MarkPageOpened(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  page_eviction_manager_.MarkPageClosed(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);

  bool called;
  storage::Status status;
  on_empty_called = false;
  page_eviction_manager_.TryEvictPages(
      policy_.get(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageEvictionManagerTest, TryEvictPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty and
  // from PageIsClosedAndSynced is |NO|.
  delegate_.closed_and_empty = PagePredicateResult::NO;
  delegate_.closed_and_synced = PagePredicateResult::NO;

  bool called;
  storage::Status status;
  PageWasEvicted was_evicted;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;
  delegate_.closed_and_synced = PagePredicateResult::YES;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedAndSynced is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  delegate_.closed_and_synced = PagePredicateResult::PAGE_OPENED;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is evicted if the result from PageIsClosedOfflineAndEmpty is
  // |YES|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  delegate_.closed_and_synced = PagePredicateResult::NO;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));

  // The page is evicted if the result from PageIsClosedAndSynced is |YES|.
  delegate_.deleted_pages.clear();
  delegate_.closed_and_empty = PagePredicateResult::NO;
  delegate_.closed_and_synced = PagePredicateResult::YES;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_POSSIBLE,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

TEST_F(PageEvictionManagerTest, EvictEmptyPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(::fuchsia::ledger::kPageIdSize, '1');

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |NO|.
  delegate_.closed_and_empty = PagePredicateResult::NO;

  bool called;
  storage::Status status;
  PageWasEvicted was_evicted;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_EMPTY,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is not evicted if the result from PageIsClosedOfflineAndEmpty is
  // |PAGE_OPENED|.
  delegate_.closed_and_empty = PagePredicateResult::PAGE_OPENED;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_EMPTY,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, IsEmpty());

  // The page is evicted if the result from PageIsClosedOfflineAndEmpty is
  // |YES|.
  delegate_.closed_and_empty = PagePredicateResult::YES;
  page_eviction_manager_.TryEvictPage(
      ledger_name, page, PageEvictionCondition::IF_EMPTY,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &was_evicted));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_TRUE(was_evicted);
  EXPECT_THAT(delegate_.deleted_pages, ElementsAre(page));
}

}  // namespace
}  // namespace ledger
