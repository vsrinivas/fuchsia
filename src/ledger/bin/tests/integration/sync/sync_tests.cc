// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/capture.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fsl/vmo/vector.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/sync/test_sync_state_watcher.h"
#include "src/ledger/bin/tests/integration/test_page_watcher.h"

namespace ledger {
namespace {

using testing::SizeIs;

class SyncIntegrationTest : public IntegrationTest {
 protected:
  std::unique_ptr<TestSyncStateWatcher> WatchPageSyncState(PagePtr* page) {
    auto watcher = std::make_unique<TestSyncStateWatcher>();
    (*page)->SetSyncStateWatcher(watcher->NewBinding());
    return watcher;
  }

  bool WaitUntilSyncIsIdle(TestSyncStateWatcher* watcher) {
    return RunLoopUntil([watcher] {
      return watcher->Equals(SyncState::IDLE, SyncState::IDLE);
    });
  }
};

// Verifies that a new page entry is correctly synchronized between two Ledger
// app instances.
//
// In this test the app instances connect to the cloud one after the other: the
// first instance uploads data to the cloud and shuts down, and only after that
// the second instance is created and connected.
TEST_P(SyncIntegrationTest, SerialConnection) {
  PageId page_id;

  // Create the first instance and write the page entry.
  auto instance1 = NewLedgerAppInstance();
  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"));

  // Retrieve the page ID so that we can later connect to the same page from
  // another app instance.
  auto loop_waiter = NewWaiter();
  page1->GetId(callback::Capture(loop_waiter->GetCallback(), &page_id));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());

  // Wait until the sync state becomes idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));

  // Create the second instance, connect to the same page and download the
  // data.
  auto instance2 = NewLedgerAppInstance();
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));
  auto page2_state_watcher = WatchPageSyncState(&page2);
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));

  PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     nullptr);

  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInlineNew_Result result;
  snapshot->GetInlineNew(
      convert::ToArray("Hello"),
      callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString("World"));

  // Verify that the sync state of the second page connection eventually becomes
  // idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
}

// Verifies that a new page entry is correctly synchronized between two Ledger
// app instances.
//
// In this test the app instances connect to the cloud concurrently: the second
// instance is already connected when the first instance writes the entry.
TEST_P(SyncIntegrationTest, ConcurrentConnection) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  PageId page_id;
  auto loop_waiter = NewWaiter();
  page1->GetId(callback::Capture(loop_waiter->GetCallback(), &page_id));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));
  auto page2_state_watcher = WatchPageSyncState(&page2);
  // Wait until the sync on the second device is idle and record the number of
  // state updates.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
  int page2_initial_state_change_count =
      page2_state_watcher->state_change_count;

  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"));

  // Wait until page1 finishes uploading the changes.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));

  // Note that we cannot just wait for the sync to become idle on the second
  // instance, as it might still be idle upon the first check because the device
  // hasn't yet received the remote notification about new commits. This is why
  // we also check that another state change notification was delivered.
  EXPECT_TRUE(
      RunLoopUntil([&page2_state_watcher, page2_initial_state_change_count] {
        return page2_state_watcher->state_change_count >
                   page2_initial_state_change_count &&
               page2_state_watcher->Equals(SyncState::IDLE, SyncState::IDLE);
      }));

  PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     nullptr);

  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInlineNew_Result result;
  snapshot->GetInlineNew(
      convert::ToArray("Hello"),
      callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString("World"));

  // Verify that the sync states of page2 eventually become idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
}

// Verifies that we download eager values in full, even if parts of these values
// were already present on disk.
//
// In this test, we connect to the page concurrently. The first connection
// uploads a big object as a LAZY value, then the second one fetches a part of
// it. After that, the first connection re-uploads the same value, but with an
// EAGER priority. When the second connection receives the changes, we verify
// that the object is fully present on disk and can be retrieved by calling Get.
TEST_P(SyncIntegrationTest, LazyToEagerTransition) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  PageId page_id;
  auto loop_waiter = NewWaiter();
  page1->GetId(callback::Capture(loop_waiter->GetCallback(), &page_id));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));

  PageSnapshotPtr snapshot;
  PageWatcherPtr watcher_ptr;
  TestPageWatcher page2_watcher(watcher_ptr.NewRequest(), []() {});
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr));

  DataGenerator generator = DataGenerator(GetRandom());

  std::vector<uint8_t> key = convert::ToArray("Hello");
  std::vector<uint8_t> big_value = generator.MakeValue(2 * 65536 + 1).take();
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromVector(big_value, &vmo));
  CreateReferenceStatus create_reference_status;
  ReferencePtr reference;
  loop_waiter = NewWaiter();
  page1->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(loop_waiter->GetCallback(), &create_reference_status,
                        &reference));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  ASSERT_EQ(CreateReferenceStatus::OK, create_reference_status);
  page1->PutReference(key, *reference, Priority::LAZY);

  EXPECT_TRUE(RunLoopUntil(
      [&page2_watcher]() { return page2_watcher.GetChangesSeen() == 1; }));
  snapshot = std::move(*page2_watcher.GetLastSnapshot());

  // Lazy value is not downloaded eagerly.
  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetNew_Result result;
  snapshot->GetNew(convert::ToArray("Hello"),
                   callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesError(fuchsia::ledger::Error::NEEDS_FETCH));

  fuchsia::ledger::PageSnapshot_FetchPartialNew_Result fetch_result;
  loop_waiter = NewWaiter();
  // Fetch only a small part.
  snapshot->FetchPartialNew(
      convert::ToArray("Hello"), 0, 10,
      callback::Capture(loop_waiter->GetCallback(), &fetch_result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(fetch_result, MatchesString(SizeIs(10)));

  // Change priority to eager, re-upload.
  page1->PutReference(key, *reference, Priority::EAGER);

  EXPECT_TRUE(RunLoopUntil(
      [&page2_watcher]() { return page2_watcher.GetChangesSeen() == 2; }));
  snapshot = std::move(*page2_watcher.GetLastSnapshot());

  // Now Get succeeds, as the value is no longer lazy.
  loop_waiter = NewWaiter();
  snapshot->GetNew(convert::ToArray("Hello"),
                   callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString(convert::ToString(big_value)));
}

// Verifies that a PageWatcher correctly delivers notifications about the
// change in case of a lazy value not already present on disk.
TEST_P(SyncIntegrationTest, PageChangeLazyEntry) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  PageId page_id;
  auto loop_waiter = NewWaiter();
  page1->GetId(callback::Capture(loop_waiter->GetCallback(), &page_id));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));
  auto page2_state_watcher = WatchPageSyncState(&page2);

  std::vector<uint8_t> key = convert::ToArray("Hello");
  std::vector<uint8_t> big_value(2 * 65536 + 1);
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromVector(big_value, &vmo));
  CreateReferenceStatus create_reference_status;
  ReferencePtr reference;
  loop_waiter = NewWaiter();
  page1->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      callback::Capture(loop_waiter->GetCallback(), &create_reference_status,
                        &reference));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  ASSERT_EQ(CreateReferenceStatus::OK, create_reference_status);
  page1->PutReference(key, *reference, Priority::LAZY);

  loop_waiter = NewWaiter();
  PageSnapshotPtr snapshot;
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest(), loop_waiter->GetCallback());
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());

  EXPECT_EQ(1u, watcher.GetChangesSeen());
  EXPECT_EQ(ResultState::COMPLETED, watcher.GetLastResultState());
  auto change = &(watcher.GetLastPageChange());
  EXPECT_EQ(1u, change->changed_entries.size());
  EXPECT_EQ(nullptr, change->changed_entries[0].value);
}

INSTANTIATE_TEST_SUITE_P(
    SyncIntegrationTest, SyncIntegrationTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()));

}  // namespace
}  // namespace ledger
