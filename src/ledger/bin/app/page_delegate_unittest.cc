// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_delegate.h"

#include <fuchsia/ledger/cpp/fidl.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

using PageDelegateTest = TestWithEnvironment;

TEST_F(PageDelegateTest, OnDiscardableInInit) {
  auto page_id = storage::PageId(::fuchsia::ledger::PAGE_ID_SIZE, 'a');
  auto storage = std::make_unique<storage::fake::FakePageStorage>(&environment_, page_id);
  auto storage_ptr = storage.get();
  auto merger = std::make_unique<MergeResolver>(
      [] {}, &environment_, storage_ptr,
      std::make_unique<ExponentialBackoff>(zx::sec(0), 1u, zx::sec(0),
                                           environment_.random()->NewBitGenerator<uint64_t>()));
  auto merger_ptr = merger.get();

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);

  PagePtr page;
  auto page_impl =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id, page.NewRequest());

  SyncWatcherSet watchers(environment_.dispatcher());

  PageDelegate delegate(&environment_, &active_page_manager, storage_ptr, merger_ptr, &watchers,
                        std::move(page_impl));

  bool on_discardable_called;
  delegate.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  // Setup is finished: let's unbind the page
  page.Unbind();
  RunLoopUntilIdle();

  bool on_done_called;
  Status status;
  delegate.Init(callback::Capture(callback::SetWhenCalled(&on_done_called), &status));

  RunLoopUntilIdle();

  EXPECT_TRUE(on_done_called);
  EXPECT_TRUE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
