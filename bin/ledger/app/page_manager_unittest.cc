// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_manager.h"

#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/storage/test/commit_contents_empty_impl.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

class PageManagerTest : public ::testing::Test {
 public:
  PageManagerTest() {}
  ~PageManagerTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(kPageIdSize, 'a');
  }

  mtl::MessageLoop message_loop_;
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
  message_loop_.task_runner()->PostDelayedTask(
      [this]() { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  message_loop_.Run();
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  PagePtr page3;
  page_manager.BindPage(GetProxy(&page3));
  page3.reset();
  message_loop_.task_runner()->PostDelayedTask(
      [this]() { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  message_loop_.Run();
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  PageSnapshotPtr snapshot;
  page_manager.BindPageSnapshot(
      std::unique_ptr<storage::CommitContents>(
          new storage::test::CommitContentsEmptyImpl()),
      GetProxy(&snapshot));
  snapshot.reset();
  message_loop_.task_runner()->PostDelayedTask(
      [this]() { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  message_loop_.Run();
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
  message_loop_.Run();
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
  message_loop_.Run();

  PageWatcherPtr watcher;
  fidl::InterfaceRequest<PageWatcher> watcher_request = GetProxy(&watcher);
  page1->Watch(std::move(watcher), [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();

  page1.reset();
  page2.reset();
  message_loop_.task_runner()->PostDelayedTask(
      [this]() { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  message_loop_.Run();
  EXPECT_FALSE(on_empty_called);

  watcher_request.PassChannel();
  message_loop_.task_runner()->PostDelayedTask(
      [this]() { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  message_loop_.Run();
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace ledger
