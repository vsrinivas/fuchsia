// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager_container.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

constexpr char kLedgerName[] = "test_ledger_name";

using PageManagerContainerTest = TestWithEnvironment;

TEST_F(PageManagerContainerTest, OneEarlyBindingNoPageManager) {
  storage::PageId page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, 'a');
  FakeDiskCleanupManager page_usage_listener;
  PagePtr page;
  bool callback_called;
  Status status;
  bool on_empty_called;

  PageManagerContainer page_manager_container(kLedgerName, page_id,
                                              &page_usage_listener);
  page_manager_container.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_manager_container.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(on_empty_called);

  page_manager_container.SetPageManager(Status::IO_ERROR, nullptr);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(Status::IO_ERROR, status);
  EXPECT_TRUE(on_empty_called);

  // We expect that the page unbinding will have no further effect.
  callback_called = false;
  on_empty_called = false;
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageManagerContainerTest, BindBeforePageManager) {
  storage::PageId page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '3');
  FakeDiskCleanupManager page_usage_listener;
  auto page_storage =
      std::make_unique<storage::fake::FakePageStorage>(&environment_, page_id);
  auto merge_resolver = std::make_unique<MergeResolver>(
      [] {}, &environment_, page_storage.get(), nullptr);
  auto page_manager = std::make_unique<PageManager>(
      &environment_, std::move(page_storage), nullptr,
      std::move(merge_resolver), PageManager::PageStorageState::AVAILABLE);
  PagePtr page;
  bool callback_called;
  Status status;
  bool on_empty_called;

  PageManagerContainer page_manager_container(kLedgerName, page_id,
                                              &page_usage_listener);
  page_manager_container.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_manager_container.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  page_manager_container.SetPageManager(Status::OK, std::move(page_manager));

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(on_empty_called);

  // We expect that the page unbinding will empty the PageManagerContainer but
  // will not cause the page's own callback to be called again.
  callback_called = false;
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace ledger
