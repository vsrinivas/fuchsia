// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/sync/test_sync_state_watcher.h"
#include "src/ledger/bin/tests/integration/test_page_watcher.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fsl/vmo/vector.h"

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
    return RunLoopUntil([watcher] { return watcher->Equals(SyncState::IDLE, SyncState::IDLE); });
  }
};

using SyncIntegrationCloudTest = SyncIntegrationTest;

// Verifies that a new page entry is correctly synchronized between two Ledger app instances.
//
// In this test the app instances connect to the cloud one after the other: the first instance
// uploads data to the cloud and shuts down, and only after that the second instance is created and
// connected.
//
// This cannot work with P2P only: the two Ledger instances are not running simulateously.
TEST_P(SyncIntegrationCloudTest, SerialConnection) {
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
  page2->GetSnapshot(snapshot.NewRequest(), {}, nullptr);

  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInline_Result result;
  snapshot->GetInline(convert::ToArray("Hello"),
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

  // Set a watcher on page2 so we are notified when page1's changes are downloaded.
  auto snpashot_waiter = NewWaiter();
  PageSnapshotPtr snapshot;
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest(), snpashot_waiter->GetCallback());
  page2->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  auto sync_waiter = NewWaiter();
  page2->Sync(sync_waiter->GetCallback());
  ASSERT_TRUE(sync_waiter->RunUntilCalled());

  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"));

  // Wait until page1 finishes uploading the changes.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));

  // Wait until page 2 sees some changes.
  ASSERT_TRUE(snpashot_waiter->RunUntilCalled());

  page2->GetSnapshot(snapshot.NewRequest(), {}, nullptr);

  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInline_Result result;
  snapshot->GetInline(convert::ToArray("Hello"),
                      callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(result, MatchesString("World"));

  // Verify that the sync states of page2 eventually become idle.
  auto page2_state_watcher = WatchPageSyncState(&page2);
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
TEST_P(SyncIntegrationTest, DISABLED_LazyToEagerTransition) {
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
  page2->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  DataGenerator generator = DataGenerator(GetRandom());

  std::vector<uint8_t> key = convert::ToArray("Hello");
  std::vector<uint8_t> big_value = generator.MakeValue(2 * 65536 + 1);
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromVector(big_value, &vmo));
  fuchsia::ledger::Page_CreateReferenceFromBuffer_Result create_result;
  loop_waiter = NewWaiter();
  page1->CreateReferenceFromBuffer(std::move(vmo).ToTransport(),
                                   callback::Capture(loop_waiter->GetCallback(), &create_result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  ASSERT_TRUE(create_result.is_response());
  page1->PutReference(key, create_result.response().reference, Priority::LAZY);

  EXPECT_TRUE(RunLoopUntil([&page2_watcher]() { return page2_watcher.GetChangesSeen() == 1; }));
  snapshot = std::move(*page2_watcher.GetLastSnapshot());

  // Lazy value is not downloaded eagerly.
  loop_waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_Get_Result get_result;
  snapshot->Get(convert::ToArray("Hello"),
                callback::Capture(loop_waiter->GetCallback(), &get_result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(get_result, MatchesError(fuchsia::ledger::Error::NEEDS_FETCH));

  fuchsia::ledger::PageSnapshot_FetchPartial_Result fetch_result;
  loop_waiter = NewWaiter();
  // Fetch only a small part.
  snapshot->FetchPartial(convert::ToArray("Hello"), 0, 10,
                         callback::Capture(loop_waiter->GetCallback(), &fetch_result));
  // TODO(LE-812): this assertion is flaky. Re-enable this test once fixed.
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(fetch_result, MatchesString(SizeIs(10)));

  // Change priority to eager, re-upload.
  page1->PutReference(key, create_result.response().reference, Priority::EAGER);

  EXPECT_TRUE(RunLoopUntil([&page2_watcher]() { return page2_watcher.GetChangesSeen() == 2; }));
  snapshot = std::move(*page2_watcher.GetLastSnapshot());

  // Now Get succeeds, as the value is no longer lazy.
  loop_waiter = NewWaiter();
  snapshot->Get(convert::ToArray("Hello"),
                callback::Capture(loop_waiter->GetCallback(), &get_result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  EXPECT_THAT(get_result, MatchesString(convert::ToString(big_value)));
}

// Verifies that a PageWatcher correctly delivers notifications about the change in case of a lazy
// value not already present on disk.
// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=12287): re-enable for P2P only once P2P
// handles large objects.
TEST_P(SyncIntegrationCloudTest, PageChangeLazyEntry) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  PageId page_id;
  auto loop_waiter = NewWaiter();
  page1->GetId(callback::Capture(loop_waiter->GetCallback(), &page_id));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  auto page2 = instance2->GetPage(fidl::MakeOptional(page_id));

  std::vector<uint8_t> key = convert::ToArray("Hello");
  std::vector<uint8_t> big_value(2 * 65536 + 1);
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromVector(big_value, &vmo));
  fuchsia::ledger::Page_CreateReferenceFromBuffer_Result result;
  loop_waiter = NewWaiter();
  page1->CreateReferenceFromBuffer(std::move(vmo).ToTransport(),
                                   callback::Capture(loop_waiter->GetCallback(), &result));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
  ASSERT_TRUE(result.is_response());

  loop_waiter = NewWaiter();
  PageSnapshotPtr snapshot;
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest(), loop_waiter->GetCallback());
  page2->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  auto sync_waiter = NewWaiter();
  page2->Sync(sync_waiter->GetCallback());
  ASSERT_TRUE(sync_waiter->RunUntilCalled());
  page1->PutReference(std::move(key), std::move(result.response().reference), Priority::LAZY);
  ASSERT_TRUE(loop_waiter->RunUntilCalled());

  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  EXPECT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(change->changed_entries[0].value, nullptr);
}

INSTANTIATE_TEST_SUITE_P(
    SyncIntegrationTest, SyncIntegrationTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(EnableSynchronization::SYNC_ONLY)),
    PrintLedgerAppInstanceFactoryBuilder());

INSTANTIATE_TEST_SUITE_P(SyncIntegrationCloudTest, SyncIntegrationCloudTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(
                             EnableSynchronization::CLOUD_SYNC_ONLY)),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
