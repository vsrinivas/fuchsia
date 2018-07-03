// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/capture.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_printf.h>

#include "garnet/public/lib/callback/capture.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace {

class PageWatcherIntegrationTest : public IntegrationTest {
 public:
  PageWatcherIntegrationTest() {}
  ~PageWatcherIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageWatcherIntegrationTest);
};

class Watcher : public ledger::PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request,
          fit::closure change_callback = [] {})
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  uint changes_seen = 0;
  ledger::ResultState last_result_state_;
  ledger::PageSnapshotPtr last_snapshot_;
  ledger::PageChange last_page_change_;

 private:
  // PageWatcher:
  void OnChange(ledger::PageChange page_change,
                ledger::ResultState result_state,
                OnChangeCallback callback) override {
    changes_seen++;
    last_result_state_ = result_state;
    last_page_change_ = std::move(page_change);
    last_snapshot_.Unbind();
    callback(last_snapshot_.NewRequest());
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  fit::closure change_callback_;
};

TEST_P(PageWatcherIntegrationTest, PageWatcherSimple) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher.last_result_state_);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectClient) {
  auto instance = NewLedgerAppInstance();
  ledger::Status status;
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  auto watcher = std::make_unique<Watcher>(watcher_ptr.NewRequest(),
                                           watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Make a change on the page and verify that it was received.
  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher->changes_seen);

  // Make another change and disconnect the watcher immediately.
  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Bob"),
            callback::Capture(waiter->GetCallback(), &status));
  watcher.reset();
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectPage) {
  auto instance = NewLedgerAppInstance();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  {
    ledger::PagePtr page = instance->GetTestPage();
    ledger::PageSnapshotPtr snapshot;
    ledger::Status status;
    auto waiter = NewWaiter();
    page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                      std::move(watcher_ptr),
                      callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);

    // Queue many put operations on the page.
    for (int i = 0; i < 1000; i++) {
      page->Put(
          convert::ToArray("name"), convert::ToArray(std::to_string(i)),
          [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
    }
  }
  // Page is out of scope now, but watcher is not. Verify that we don't crash
  // and a change notification is still delivered.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDelete) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  auto waiter = NewWaiter();
  ledger::Status status;
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  auto watcher_waiter = NewWaiter();
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  waiter = NewWaiter();
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Delete(convert::ToArray("foo"),
               callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  ASSERT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher.last_result_state_);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  EXPECT_EQ(0u, change.changed_entries->size());
  ASSERT_EQ(1u, change.deleted_keys->size());
  EXPECT_EQ("foo", convert::ToString(change.deleted_keys->at(0)));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeSize) {
  auto instance = NewLedgerAppInstance();
  // Put enough entries to ensure we will need more than one query to retrieve
  // them. The number of entries that can be retrieved in one query is bound by
  // |kMaxMessageHandles| and by size of the fidl message (determined by
  // |kMaxInlineDataSize|), so we insert one entry more than that.
  const size_t key_size = ledger::kMaxKeySize;
  const size_t entry_size = ledger::fidl_serialization::GetEntrySize(key_size);
  const size_t entry_count =
      std::min(ledger::fidl_serialization::kMaxMessageHandles,
               ledger::fidl_serialization::kMaxInlineDataSize / entry_size) +
      1;
  const auto key_generator = [](size_t i) {
    std::string prefix = fxl::StringPrintf("key%03" PRIuMAX, i);
    std::string filler(key_size - prefix.size(), 'k');
    return prefix + filler;
  };
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  for (size_t i = 0; i < entry_count; ++i) {
    waiter = NewWaiter();
    page->Put(convert::ToArray(key_generator(i)), convert::ToArray("value"),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_EQ(0u, watcher.changes_seen);

  waiter = NewWaiter();
  page->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Get the first OnChagne call.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ledger::ResultState::PARTIAL_STARTED);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  size_t initial_size = change.changed_entries->size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(key_generator(i),
              convert::ToString(change.changed_entries->at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries->at(i).value));
    EXPECT_EQ(ledger::Priority::EAGER, change.changed_entries->at(i).priority);
  }

  // Get the second OnChagne call.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change.changed_entries->size());
  for (size_t i = 0; i < change.changed_entries->size(); ++i) {
    EXPECT_EQ(key_generator(i + initial_size),
              convert::ToString(change.changed_entries->at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries->at(i).value));
    EXPECT_EQ(ledger::Priority::EAGER, change.changed_entries->at(i).priority);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeHandles) {
  auto instance = NewLedgerAppInstance();
  size_t entry_count = 70;
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  for (size_t i = 0; i < entry_count; ++i) {
    waiter = NewWaiter();
    page->Put(convert::ToArray(fxl::StringPrintf("key%02" PRIuMAX, i)),
              convert::ToArray("value"),
              callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(ledger::Status::OK, status);
  }

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_EQ(0u, watcher.changes_seen);

  waiter = NewWaiter();
  page->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Get the first OnChagne call.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ledger::ResultState::PARTIAL_STARTED);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  size_t initial_size = change.changed_entries->size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(fxl::StringPrintf("key%02" PRIuMAX, i),
              convert::ToString(change.changed_entries->at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries->at(i).value));
    EXPECT_EQ(ledger::Priority::EAGER, change.changed_entries->at(i).priority);
  }

  // Get the second OnChagne call.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change.changed_entries->size());
  for (size_t i = 0; i < change.changed_entries->size(); ++i) {
    EXPECT_EQ(fxl::StringPrintf("key%02" PRIuMAX, i + initial_size),
              convert::ToString(change.changed_entries->at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries->at(i).value));
    EXPECT_EQ(ledger::Priority::EAGER, change.changed_entries->at(i).priority);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherSnapshot) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  ledger::Status status;
  auto waiter = NewWaiter();
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher.last_result_state_);
  auto entries = SnapshotGetEntries(this, &(watcher.last_snapshot_));
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ("name", convert::ToString(entries[0].key));
  EXPECT_EQ("Alice", ToString(entries[0].value));
  EXPECT_EQ(ledger::Priority::EAGER, entries[0].priority);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherTransaction) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  ledger::Status status;
  auto waiter = NewWaiter();
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_EQ(0u, watcher.changes_seen);

  waiter = NewWaiter();
  page->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher.last_result_state_);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherParallel) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  ledger::PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();

  ledger::PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), ledger::Status::OK);

  ledger::PageWatcherPtr watcher1_ptr;
  auto watcher_waiter1 = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher_waiter1->GetCallback());
  ledger::PageSnapshotPtr snapshot1;
  ledger::Status status;
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher1_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  ledger::PageWatcherPtr watcher2_ptr;
  auto watcher_waiter2 = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher_waiter2->GetCallback());
  ledger::PageSnapshotPtr snapshot2;
  waiter = NewWaiter();
  page2->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher2_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  // Verify that each change is seen by the right watcher.
  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter1->RunUntilCalled();
  EXPECT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher1.last_result_state_);
  ledger::PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(0).value));

  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter2->RunUntilCalled();
  EXPECT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));

  // A merge happens now. Only the first watcher should see a change.
  watcher_waiter1->RunUntilCalled();
  EXPECT_EQ(2u, watcher1.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher2.last_result_state_);
  EXPECT_EQ(1u, watcher2.changes_seen);

  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherEmptyTransaction) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_EQ(0u, watcher.changes_seen);
}

TEST_P(PageWatcherIntegrationTest, PageWatcher1Change2Pages) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  ledger::PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();

  ledger::PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), ledger::Status::OK);

  ledger::PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());
  ledger::PageSnapshotPtr snapshot1;
  waiter = NewWaiter();
  ledger::Status status;
  page1->GetSnapshot(snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher1_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  auto watcher2_waiter = NewWaiter();
  ledger::PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());
  ledger::PageSnapshotPtr snapshot2;
  waiter = NewWaiter();
  page2->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher2_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher1_waiter->RunUntilCalled();
  watcher2_waiter->RunUntilCalled();

  ASSERT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher1.last_result_state_);
  ledger::PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(0).value));

  ASSERT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(0).value));
}

class WaitingWatcher : public ledger::PageWatcher {
 public:
  WaitingWatcher(fidl::InterfaceRequest<ledger::PageWatcher> request,
                 fit::closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  struct Change {
    ledger::PageChange change;
    OnChangeCallback callback;

    Change(ledger::PageChange change, OnChangeCallback callback)
        : change(std::move(change)), callback(std::move(callback)) {}
  };

  std::vector<Change> changes;

 private:
  // PageWatcher:
  void OnChange(ledger::PageChange page_change,
                ledger::ResultState result_state,
                OnChangeCallback callback) override {
    FXL_DCHECK(result_state == ledger::ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes.emplace_back(std::move(page_change), std::move(callback));
    change_callback_();
  }

  fidl::Binding<ledger::PageWatcher> binding_;
  fit::closure change_callback_;
};

TEST_P(PageWatcherIntegrationTest, PageWatcherConcurrentTransaction) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  WaitingWatcher watcher(watcher_ptr.NewRequest(),
                         watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes.size());

  waiter = NewWaiter();
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"),
            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  auto transaction_waiter = NewWaiter();
  ledger::Status start_transaction_status;
  page->StartTransaction(callback::Capture(transaction_waiter->GetCallback(),
                                           &start_transaction_status));

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(1u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[0].callback(nullptr);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(100)));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[1].callback(nullptr);

  transaction_waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, start_transaction_status);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefix) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"),

            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page->Put(convert::ToArray("01-key"), convert::ToArray("value-01"),

            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page->Put(convert::ToArray("02-key"), convert::ToArray("value-02"),

            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  waiter = NewWaiter();
  page->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ledger::ResultState::COMPLETED, watcher.last_result_state_);
  ledger::PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("01-key", convert::ToString(change.changed_entries->at(0).key));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefixNoChange) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  ledger::PageSnapshotPtr snapshot;
  auto waiter = NewWaiter();
  ledger::Status status;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"),
                    std::move(watcher_ptr),
                    callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"),

            callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  waiter = NewWaiter();
  page->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  EXPECT_EQ(ledger::Status::OK, status);

  // Starting a transaction drains all watcher notifications, so if we were to
  // be called, we would know at this point.
  EXPECT_EQ(0u, watcher.changes_seen);
}

INSTANTIATE_TEST_CASE_P(PageWatcherIntegrationTest, PageWatcherIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace integration
}  // namespace test
