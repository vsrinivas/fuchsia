// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_manager.h"

#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/cloud_sync/public/ledger_sync.h"
#include "apps/ledger/src/cloud_sync/test/page_sync_empty_impl.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/storage/test/commit_contents_empty_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

class FakePageSync : public cloud_sync::test::PageSyncEmptyImpl {
 public:
  void Start() { start_called = true; }

  void SetOnBacklogDownloaded(ftl::Closure on_backlog_downloaded_callback) {
    this->on_backlog_downloaded_callback =
        std::move(on_backlog_downloaded_callback);
  }

  bool start_called = false;
  ftl::Closure on_backlog_downloaded_callback;
};

class PageManagerTest : public test::TestWithMessageLoop {
 public:
  PageManagerTest() {}
  ~PageManagerTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(kPageIdSize, 'a');
  }

  storage::PageId page_id_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageManagerTest);
};

TEST_F(PageManagerTest, OnEmptyCallback) {
  bool on_empty_called = false;
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  PageManager page_manager(std::move(storage), nullptr);
  page_manager.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(on_empty_called);
  PagePtr page1;
  PagePtr page2;
  page_manager.BindPage(GetProxy(&page1));
  page_manager.BindPage(GetProxy(&page2));
  page1.reset();
  page2.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  PagePtr page3;
  page_manager.BindPage(GetProxy(&page3));
  page3.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  PageSnapshotPtr snapshot;
  page_manager.BindPageSnapshot(
      std::unique_ptr<storage::CommitContents>(
          new storage::test::CommitContentsEmptyImpl()),
      GetProxy(&snapshot));
  snapshot.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageManagerTest, DeletingPageManagerClosesConnections) {
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto page_manager =
      std::make_unique<PageManager>(std::move(storage), nullptr);

  PagePtr page;
  page_manager->BindPage(GetProxy(&page));
  bool page_closed = false;
  page.set_connection_error_handler([this, &page_closed] {
    page_closed = true;
    message_loop_.PostQuitTask();
  });

  page_manager.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(page_closed);
}

TEST_F(PageManagerTest, OnEmptyCallbackWithWatcher) {
  bool on_empty_called = false;
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  PageManager page_manager(std::move(storage), nullptr);
  page_manager.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(on_empty_called);
  PagePtr page1;
  PagePtr page2;
  page_manager.BindPage(GetProxy(&page1));
  page_manager.BindPage(GetProxy(&page2));
  page1->Put(convert::ToArray("key1"), convert::ToArray("value1"),
             [this](Status status) {
               EXPECT_EQ(Status::OK, status);
               message_loop_.PostQuitTask();
             });
  EXPECT_FALSE(RunLoopWithTimeout());

  PageWatcherPtr watcher;
  fidl::InterfaceRequest<PageWatcher> watcher_request = GetProxy(&watcher);
  page1->Watch(std::move(watcher), [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  page1.reset();
  page2.reset();
  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_FALSE(on_empty_called);

  watcher_request.PassChannel();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageManagerTest, DelayBindingUntilSyncBacklogDownloaded) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto page_sync_context = std::make_unique<cloud_sync::PageSyncContext>();
  page_sync_context->page_sync = std::move(fake_page_sync);
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);

  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  PageManager page_manager(std::move(storage), std::move(page_sync_context));

  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called = false;
  PagePtr page;
  page_manager.BindPage(GetProxy(&page));
  page->GetId([this, &called](fidl::Array<uint8_t> id) {
    called = true;
    message_loop_.PostQuitTask();
  });

  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_FALSE(called);

  fake_page_sync_ptr->on_backlog_downloaded_callback();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(called);

  // Check that a second call on the same manager is not delayed.
  called = false;
  page.reset();
  page_manager.BindPage(GetProxy(&page));
  page->GetId([this, &called](fidl::Array<uint8_t> id) {
    called = true;
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(called);
};

}  // namespace
}  // namespace ledger
