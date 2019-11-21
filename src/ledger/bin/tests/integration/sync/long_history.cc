// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/optional.h>

#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/sync/test_sync_state_watcher.h"
#include "src/ledger/bin/tests/integration/test_page_watcher.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/waiter.h"

namespace ledger {
namespace {

class LongHistorySyncTest : public IntegrationTest {
 protected:
  std::unique_ptr<TestSyncStateWatcher> WatchPageSyncState(PagePtr* page) {
    auto watcher = std::make_unique<TestSyncStateWatcher>();
    (*page)->SetSyncStateWatcher(watcher->NewBinding());
    return watcher;
  }

  bool WaitUntilSyncIsIdle(TestSyncStateWatcher* watcher) {
    return RunLoopUntil([watcher] { return watcher->Equals(SyncState::IDLE, SyncState::IDLE); });
  }
};

TEST_P(LongHistorySyncTest, SyncLongHistory) {
  PageId page_id;

  // Create the first instance and write the page entries.
  auto instance1 = NewLedgerAppInstance();
  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  ASSERT_TRUE(page1_state_watcher);
  const int commit_history_length = 500;
  // Overwrite one key N times, creating N implicit commits.
  for (int i = 0; i < commit_history_length; i++) {
    page1->Put(convert::ToArray("iteration"), convert::ToArray(std::to_string(i)));
  }
  // Wait until the commits are uploaded.
  auto waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));

  // Retrieve the page ID so that we can later connect to the same page from
  // another app instance.
  waiter = NewWaiter();
  page1->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Create the second instance, connect to the same page and download the
  // data.
  auto instance2 = NewLedgerAppInstance();
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));
  // Wait until we get up-to-date data: read a snapshot. If it already has a key "iteration", we are
  // done. Otherwise, wait until its watcher signals a change.
  PageSnapshotPtr snapshot;
  PageWatcherPtr watcher_ptr;
  auto snapshot_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), snapshot_waiter->GetCallback());
  page2->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInline_Result result;
  snapshot->GetInline(convert::ToArray("iteration"),
                      callback::Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  if (result.is_err()) {
    // The key is not present yet, wait for an event on the watcher.
    EXPECT_EQ(result.err(), Error::KEY_NOT_FOUND);
    ASSERT_TRUE(snapshot_waiter->RunUntilCalled());
  }

  page2->GetSnapshot(snapshot.NewRequest(), {}, nullptr);

  waiter = NewWaiter();
  snapshot->GetInline(convert::ToArray("iteration"),
                      callback::Capture(waiter->GetCallback(), &result));
  ASSERT_TRUE(waiter->RunUntilCalled());
  const int last_iteration = commit_history_length - 1;
  ASSERT_THAT(result, MatchesString(std::to_string(last_iteration)));

  // Verify that the sync state of the second page connection eventually becomes
  // idle.
  auto page2_state_watcher = WatchPageSyncState(&page2);
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
}

INSTANTIATE_TEST_SUITE_P(
    LongHistorySyncTest, LongHistorySyncTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(EnableSynchronization::SYNC_ONLY)),
    PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
