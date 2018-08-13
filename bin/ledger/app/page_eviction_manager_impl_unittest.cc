// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class FakeDelegate : public PageEvictionManager::Delegate {
 public:
  void PageIsClosedAndSynced(
      fxl::StringView /*ledger_name*/, storage::PageIdView /*page_id*/,
      fit::function<void(Status, PageClosedAndSynced)> callback) override {
    callback(page_closed_and_synced_status, closed_and_synced);
  }

  void DeletePageStorage(fxl::StringView /*ledger_name*/,
                         storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override {
    deleted_pages_.push_back(page_id.ToString());
    callback(Status::OK);
  }

  std::vector<storage::PageId> deleted_pages_;

  PageClosedAndSynced closed_and_synced = PageClosedAndSynced::YES;
  Status page_closed_and_synced_status = Status::OK;
};

class PageEvictionManagerTest : public gtest::TestLoopFixture {
 public:
  PageEvictionManagerTest()
      : page_eviction_manager_(dispatcher(), &coroutine_service_,
                               ledger::DetachedPath(tmpfs_.root_fd())) {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    EXPECT_EQ(Status::OK, page_eviction_manager_.Init());
    RunLoopUntilIdle();
    page_eviction_manager_.SetDelegate(&delegate_);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  coroutine::CoroutineServiceImpl coroutine_service_;

 protected:
  FakeDelegate delegate_;
  PageEvictionManagerImpl page_eviction_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerTest);
};

TEST_F(PageEvictionManagerTest, NoEvictionWithoutPages) {
  bool called;
  Status status;

  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, IsEmpty());
}

TEST_F(PageEvictionManagerTest, AtLeastOneEvictionWhenPossible) {
  fxl::StringView ledger_name = "ledger";
  storage::PageIdView page1 = std::string(kPageIdSize, '1');
  storage::PageIdView page2 = std::string(kPageIdSize, '2');

  delegate_.closed_and_synced = PageClosedAndSynced::YES;

  page_eviction_manager_.OnPageOpened(ledger_name, page1);
  page_eviction_manager_.OnPageClosed(ledger_name, page1);
  page_eviction_manager_.OnPageOpened(ledger_name, page2);
  page_eviction_manager_.OnPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(delegate_.deleted_pages_.empty());
}

TEST_F(PageEvictionManagerTest, DontEvictUnsyncedPages) {
  std::string ledger_name = "ledger";
  storage::PageId page1 = std::string(kPageIdSize, '1');
  storage::PageId page2 = std::string(kPageIdSize, '2');

  delegate_.closed_and_synced = PageClosedAndSynced::NO;

  page_eviction_manager_.OnPageOpened(ledger_name, page1);
  page_eviction_manager_.OnPageClosed(ledger_name, page1);
  page_eviction_manager_.OnPageOpened(ledger_name, page2);
  page_eviction_manager_.OnPageClosed(ledger_name, page2);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, IsEmpty());
}

TEST_F(PageEvictionManagerTest, DontEvictOpenPages) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(kPageIdSize, '1');

  delegate_.closed_and_synced = PageClosedAndSynced::YES;

  page_eviction_manager_.OnPageOpened(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, IsEmpty());

  // Close the page. It can now be evicted.
  page_eviction_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, ElementsAre(page));
}

TEST_F(PageEvictionManagerTest, DontEvictAnEvictedPage) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(kPageIdSize, '1');

  delegate_.closed_and_synced = PageClosedAndSynced::YES;

  page_eviction_manager_.OnPageOpened(ledger_name, page);
  page_eviction_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  bool called;
  Status status;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, ElementsAre(page));

  delegate_.deleted_pages_.clear();
  // Try to clean up again. We shouldn't be able to evict any pages.
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, IsEmpty());
}

TEST_F(PageEvictionManagerTest, PageNotFoundIsNotAnError) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(kPageIdSize, '1');

  delegate_.closed_and_synced = PageClosedAndSynced::YES;

  page_eviction_manager_.OnPageOpened(ledger_name, page);
  page_eviction_manager_.OnPageClosed(ledger_name, page);
  RunLoopUntilIdle();

  delegate_.page_closed_and_synced_status = Status::PAGE_NOT_FOUND;

  bool called;
  Status status;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate_.deleted_pages_, IsEmpty());
}

TEST_F(PageEvictionManagerTest, IsEmpty) {
  std::string ledger_name = "ledger";
  storage::PageId page = std::string(kPageIdSize, '1');
  bool on_empty_called = false;

  page_eviction_manager_.set_on_empty([&] { on_empty_called = true; });

  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);

  // PageEvictionManagerImpl should be empty if there is no pending operation
  // on: OnPageOpened, OnPageClosed, or TryCleanUp.
  on_empty_called = false;
  page_eviction_manager_.OnPageOpened(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  page_eviction_manager_.OnPageClosed(ledger_name, page);
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);

  bool called;
  Status status;
  on_empty_called = false;
  page_eviction_manager_.TryCleanUp(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_FALSE(page_eviction_manager_.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(page_eviction_manager_.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace ledger
