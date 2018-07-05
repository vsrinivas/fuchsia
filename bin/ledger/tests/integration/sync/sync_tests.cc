// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/capture.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace sync {
namespace {

class SyncWatcherImpl : public ledger::SyncWatcher {
 public:
  SyncWatcherImpl() : binding_(this) {}
  ~SyncWatcherImpl() override {}

  auto NewBinding() { return binding_.NewBinding(); }

  bool Equals(ledger::SyncState download, ledger::SyncState upload) {
    return download == download_state && upload == upload_state;
  }

  ledger::SyncState download_state = ledger::SyncState::PENDING;
  ledger::SyncState upload_state = ledger::SyncState::PENDING;
  int state_change_count = 0;

 private:
  // ledger::SyncWatcher:
  void SyncStateChanged(ledger::SyncState download, ledger::SyncState upload,
                        SyncStateChangedCallback callback) override {
    state_change_count++;
    download_state = download;
    upload_state = upload;
    callback();
  }

  fidl::Binding<ledger::SyncWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

class SyncIntegrationTest : public IntegrationTest {
 protected:
  std::unique_ptr<SyncWatcherImpl> WatchPageSyncState(ledger::PagePtr* page) {
    auto watcher = std::make_unique<SyncWatcherImpl>();

    ledger::Status status = ledger::Status::INTERNAL_ERROR;
    (*page)->SetSyncStateWatcher(watcher->NewBinding(),
                                 callback::Capture(QuitLoopClosure(), &status));
    RunLoop();
    EXPECT_EQ(ledger::Status::OK, status);

    return watcher;
  }

  bool WaitUntilSyncIsIdle(SyncWatcherImpl* watcher) {
    return RunLoopUntil([watcher] {
      return watcher->Equals(ledger::SyncState::IDLE, ledger::SyncState::IDLE);
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
  ledger::PageId page_id;
  ledger::Status status;

  // Create the first instance and write the page entry.
  auto instance1 = NewLedgerAppInstance();
  auto page1 = instance1->GetTestPage();
  auto page1_state_watcher = WatchPageSyncState(&page1);
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"),
             callback::Capture(QuitLoopClosure(), &status));
  RunLoop();

  // Retrieve the page ID so that we can later connect to the same page from
  // another app instance.
  ASSERT_EQ(ledger::Status::OK, status);
  page1->GetId(callback::Capture(QuitLoopClosure(), &page_id));
  RunLoop();

  // Wait until the sync state becomes idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));

  // Create the second instance, connect to the same page and download the
  // data.
  auto instance2 = NewLedgerAppInstance();
  auto page2 =
      instance2->GetPage(fidl::MakeOptional(page_id), ledger::Status::OK);
  auto page2_state_watcher = WatchPageSyncState(&page2);
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));

  ledger::PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     nullptr, callback::Capture(QuitLoopClosure(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  std::unique_ptr<ledger::InlinedValue> inlined_value;

  snapshot->GetInline(
      convert::ToArray("Hello"),
      callback::Capture(QuitLoopClosure(), &status, &inlined_value));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  ASSERT_TRUE(inlined_value);
  ASSERT_EQ("World", convert::ToString(inlined_value->value));

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
  ledger::PageId page_id;
  page1->GetId(callback::Capture(QuitLoopClosure(), &page_id));
  RunLoop();
  auto page2 =
      instance2->GetPage(fidl::MakeOptional(page_id), ledger::Status::OK);
  auto page2_state_watcher = WatchPageSyncState(&page2);
  // Wait until the sync on the second device is idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
  int page2_initial_state_change_count =
      page2_state_watcher->state_change_count;

  ledger::Status status;
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"),
             callback::Capture(QuitLoopClosure(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  // Note that we cannot just wait for the sync to become idle on the second
  // instance, as it might still be idle upon the first check because the device
  // hasn't yet received the remote notification about new commits. This is why
  // we also check that another state change notification was delivered.
  EXPECT_TRUE(
      RunLoopUntil([&page2_state_watcher, page2_initial_state_change_count] {
        return page2_state_watcher->state_change_count >
                   page2_initial_state_change_count &&
               page2_state_watcher->Equals(ledger::SyncState::IDLE,
                                           ledger::SyncState::IDLE);
      }));

  ledger::PageSnapshotPtr snapshot;
  page2->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     nullptr, callback::Capture(QuitLoopClosure(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  std::unique_ptr<ledger::InlinedValue> inlined_value;
  snapshot->GetInline(
      convert::ToArray("Hello"),
      callback::Capture(QuitLoopClosure(), &status, &inlined_value));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);
  ASSERT_TRUE(inlined_value);
  ASSERT_EQ("World", convert::ToString(inlined_value->value));

  // Verify that the sync states of both pages eventually become idle.
  EXPECT_TRUE(WaitUntilSyncIsIdle(page1_state_watcher.get()));
  EXPECT_TRUE(WaitUntilSyncIsIdle(page2_state_watcher.get()));
}

INSTANTIATE_TEST_CASE_P(SyncIntegrationTest, SyncIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace sync
}  // namespace integration
}  // namespace test
