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

 private:
  // ledger::SyncWatcher:
  void SyncStateChanged(ledger::SyncState download, ledger::SyncState upload,
                        SyncStateChangedCallback callback) override {
    download_state = download;
    upload_state = upload;
    callback();
  }

  fidl::Binding<ledger::SyncWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

class SyncIntegrationTest : public IntegrationTest {
 protected:
  ::testing::AssertionResult GetEntries(
      ledger::Page* page, fidl::VectorPtr<ledger::Entry>* entries) {
    ledger::PageSnapshotPtr snapshot;
    ledger::Status status;
    page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                      nullptr, callback::Capture(QuitLoopClosure(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure() << "Unable to retrieve a snapshot";
    }
    entries->resize(0);
    std::unique_ptr<ledger::Token> token = nullptr;
    std::unique_ptr<ledger::Token> next_token = nullptr;
    do {
      fidl::VectorPtr<ledger::Entry> new_entries;
      snapshot->GetEntries(fidl::VectorPtr<uint8_t>::New(0), std::move(token),
                           callback::Capture(QuitLoopClosure(), &status,
                                             &new_entries, &next_token));
      RunLoop();
      if (status != ledger::Status::OK) {
        return ::testing::AssertionFailure() << "Unable to retrieve entries";
      }
      for (auto& new_entry : *new_entries) {
        entries->push_back(std::move(new_entry));
      }
      token = std::move(next_token);
    } while (token);
    return ::testing::AssertionSuccess();
  }

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

  // TODO(ppi): just wait until sync is idle rather than retrieving entries when
  // LE-369 is fixed. Before that we can get the sync state of IDLE, IDLE before
  // any synchronization happens.
  EXPECT_TRUE(RunLoopUntil([this, &page2] {
    fidl::VectorPtr<ledger::Entry> entries;
    if (!GetEntries(page2.get(), &entries)) {
      return true;
    }
    return !entries->empty();
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

  ledger::Status status;
  page1->Put(convert::ToArray("Hello"), convert::ToArray("World"),
             callback::Capture(QuitLoopClosure(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  // TODO(ppi): just wait until sync is idle rather than retrieving entries when
  // LE-369 is fixed. Before that we can get the sync state of IDLE, IDLE before
  // any synchronization happens.
  EXPECT_TRUE(RunLoopUntil([this, &page2] {
    fidl::VectorPtr<ledger::Entry> entries;
    if (!GetEntries(page2.get(), &entries)) {
      return true;
    }
    return !entries->empty();
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
