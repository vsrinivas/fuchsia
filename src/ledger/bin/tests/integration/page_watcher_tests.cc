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
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/public/lib/callback/capture.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"

namespace ledger {
namespace {

using ::testing::SizeIs;

class PageWatcherIntegrationTest : public IntegrationTest {
 public:
  PageWatcherIntegrationTest() {}
  ~PageWatcherIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageWatcherIntegrationTest);
};

class Watcher : public PageWatcher {
 public:
  explicit Watcher(
      fidl::InterfaceRequest<PageWatcher> request,
      fit::closure change_callback = [] {})
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  void DelayCallback(bool delay_callback) { delay_callback_ = delay_callback; }

  void CallOnChangeCallback() {
    FXL_CHECK(on_change_callback_);
    on_change_callback_(last_snapshot_.NewRequest());
    on_change_callback_ = nullptr;
  }

  uint changes_seen = 0;
  ResultState last_result_state_;
  PageSnapshotPtr last_snapshot_;
  PageChange last_page_change_;

 private:
  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override {
    changes_seen++;
    last_result_state_ = result_state;
    last_page_change_ = std::move(page_change);
    last_snapshot_.Unbind();
    FXL_CHECK(!on_change_callback_);
    on_change_callback_ = std::move(callback);
    if (!delay_callback_) {
      CallOnChangeCallback();
    }
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  bool delay_callback_ = false;
  OnChangeCallback on_change_callback_;
  fit::closure change_callback_;
};

TEST_P(PageWatcherIntegrationTest, PageWatcherSimple) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries.at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherAggregatedNotifications) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  // Call Put and don't let the OnChange callback be called, yet.
  watcher.DelayCallback(true);
  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->PutNew(convert::ToArray("key"), convert::ToArray("value1"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  auto changed_entries = std::move(watcher.last_page_change_.changed_entries);
  ASSERT_THAT(changed_entries, SizeIs(1));
  EXPECT_EQ("key", convert::ToString(changed_entries.at(0).key));
  EXPECT_EQ("value1", ToString(changed_entries.at(0).value));

  // Update the value of "key" initially to "value2" and then to "value3".
  page->PutNew(convert::ToArray("key"), convert::ToArray("value2"));
  page->PutNew(convert::ToArray("key"), convert::ToArray("value3"));

  // Since the previous OnChange callback hasn't been called yet, the next
  // notification should be blocked.
  ASSERT_FALSE(watcher_waiter->RunUntilCalled());

  // Call the OnChange callback and expect a new OnChange call.
  watcher.CallOnChangeCallback();
  watcher.DelayCallback(false);
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  // Only the last value of "key" should be found in the changed entries set.
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  changed_entries = std::move(watcher.last_page_change_.changed_entries);
  ASSERT_THAT(changed_entries, SizeIs(1));
  EXPECT_EQ("key", convert::ToString(changed_entries.at(0).key));
  EXPECT_EQ("value3", ToString(changed_entries.at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectClient) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  auto watcher = std::make_unique<Watcher>(watcher_ptr.NewRequest(),
                                           watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  // Make a change on the page and verify that it was received.
  page->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher->changes_seen);

  // Make another change and disconnect the watcher immediately.
  page->PutNew(convert::ToArray("name"), convert::ToArray("Bob"));
  watcher.reset();
  auto waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectPage) {
  auto instance = NewLedgerAppInstance();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  {
    PagePtr page = instance->GetTestPage();
    PageSnapshotPtr snapshot;
    page->GetSnapshotNew(snapshot.NewRequest(),
                         fidl::VectorPtr<uint8_t>::New(0),
                         std::move(watcher_ptr));

    // Queue many put operations on the page.
    for (int i = 0; i < 1000; i++) {
      page->PutNew(convert::ToArray("name"),
                   convert::ToArray(std::to_string(i)));
    }
  }
  // Page is out of scope now, but watcher is not. Verify that we don't crash
  // and a change notification is still delivered.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDelete) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  page->PutNew(convert::ToArray("foo"), convert::ToArray("bar"));

  auto watcher_waiter = NewWaiter();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));

  page->DeleteNew(convert::ToArray("foo"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  ASSERT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChange change = std::move(watcher.last_page_change_);
  EXPECT_EQ(0u, change.changed_entries.size());
  ASSERT_EQ(1u, change.deleted_keys.size());
  EXPECT_EQ("foo", convert::ToString(change.deleted_keys.at(0)));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeSize) {
  auto instance = NewLedgerAppInstance();
  // Put enough entries to ensure we will need more than one query to retrieve
  // them. The number of entries that can be retrieved in one query is bound by
  // |kMaxMessageHandles| and by size of the fidl message (determined by
  // |kMaxInlineDataSize|), so we insert one entry more than that.
  const size_t key_size = kMaxKeySize;
  const size_t entry_size = fidl_serialization::GetEntrySize(key_size);
  const size_t entry_count =
      std::min(fidl_serialization::kMaxMessageHandles,
               fidl_serialization::kMaxInlineDataSize / entry_size) +
      1;
  const auto key_generator = [](size_t i) {
    std::string prefix = fxl::StringPrintf("key%03" PRIuMAX, i);
    std::string filler(key_size - prefix.size(), 'k');
    return prefix + filler;
  };
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->StartTransactionNew();
  for (size_t i = 0; i < entry_count; ++i) {
    page->PutNew(convert::ToArray(key_generator(i)), convert::ToArray("value"));
  }

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(0u, watcher.changes_seen);

  page->CommitNew();

  // Get the first OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ResultState::PARTIAL_STARTED);
  PageChange change = std::move(watcher.last_page_change_);
  size_t initial_size = change.changed_entries.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(key_generator(i),
              convert::ToString(change.changed_entries.at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries.at(i).value));
    EXPECT_EQ(Priority::EAGER, change.changed_entries.at(i).priority);
  }

  // Get the second OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change.changed_entries.size());
  for (size_t i = 0; i < change.changed_entries.size(); ++i) {
    EXPECT_EQ(key_generator(i + initial_size),
              convert::ToString(change.changed_entries.at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries.at(i).value));
    EXPECT_EQ(Priority::EAGER, change.changed_entries.at(i).priority);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeHandles) {
  auto instance = NewLedgerAppInstance();
  size_t entry_count = 70;
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->StartTransactionNew();
  for (size_t i = 0; i < entry_count; ++i) {
    page->PutNew(convert::ToArray(fxl::StringPrintf("key%02" PRIuMAX, i)),
                 convert::ToArray("value"));
  }

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(0u, watcher.changes_seen);

  page->CommitNew();

  // Get the first OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ResultState::PARTIAL_STARTED);
  PageChange change = std::move(watcher.last_page_change_);
  size_t initial_size = change.changed_entries.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(fxl::StringPrintf("key%02" PRIuMAX, i),
              convert::ToString(change.changed_entries.at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries.at(i).value));
    EXPECT_EQ(Priority::EAGER, change.changed_entries.at(i).priority);
  }

  // Get the second OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change.changed_entries.size());
  for (size_t i = 0; i < change.changed_entries.size(); ++i) {
    EXPECT_EQ(fxl::StringPrintf("key%02" PRIuMAX, i + initial_size),
              convert::ToString(change.changed_entries.at(i).key));
    EXPECT_EQ("value", ToString(change.changed_entries.at(i).value));
    EXPECT_EQ(Priority::EAGER, change.changed_entries.at(i).priority);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherSnapshot) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  auto entries = SnapshotGetEntries(this, &(watcher.last_snapshot_));
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ("name", convert::ToString(entries[0].key));
  EXPECT_EQ("Alice", ToString(entries[0].value));
  EXPECT_EQ(Priority::EAGER, entries[0].priority);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherTransaction) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->StartTransactionNew();
  page->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(0u, watcher.changes_seen);

  page->CommitNew();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries.at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherParallel) {
  auto instance = NewLedgerAppInstance();
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  PageWatcherPtr watcher1_ptr;
  auto watcher_waiter1 = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher_waiter1->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshotNew(snapshot1.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  auto watcher_waiter2 = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher_waiter2->GetCallback());
  PageSnapshotPtr snapshot2;
  page2->GetSnapshotNew(snapshot2.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher2_ptr));
  page1->StartTransactionNew();
  page1->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page2->StartTransactionNew();
  page2->PutNew(convert::ToArray("name"), convert::ToArray("Bob"));

  // Verify that each change is seen by the right watcher.
  page1->CommitNew();

  ASSERT_TRUE(watcher_waiter1->RunUntilCalled());
  EXPECT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher1.last_result_state_);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries.at(0).value));

  page2->CommitNew();

  ASSERT_TRUE(watcher_waiter2->RunUntilCalled());
  EXPECT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries.at(0).value));

  RunLoopFor(zx::msec(100));

  // A merge happens now. Only the first watcher should see a change.
  ASSERT_TRUE(watcher_waiter1->RunUntilCalled());
  EXPECT_EQ(2u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  EXPECT_EQ(1u, watcher2.changes_seen);

  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries.at(0).value));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherEmptyTransaction) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->StartTransactionNew();
  page->CommitNew();

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(0u, watcher.changes_seen);
}

TEST_P(PageWatcherIntegrationTest, PageWatcher1Change2Pages) {
  auto instance = NewLedgerAppInstance();
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshotNew(snapshot1.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher1_ptr));

  auto watcher2_waiter = NewWaiter();
  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page2->GetSnapshotNew(snapshot2.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher2_ptr));

  page1->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());

  ASSERT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher1.last_result_state_);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries.at(0).value));

  ASSERT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries.at(0).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries.at(0).value));
}

class WaitingWatcher : public PageWatcher {
 public:
  WaitingWatcher(fidl::InterfaceRequest<PageWatcher> request,
                 fit::closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  struct Change {
    PageChange change;
    OnChangeCallback callback;

    Change(PageChange change, OnChangeCallback callback)
        : change(std::move(change)), callback(std::move(callback)) {}
  };

  std::vector<Change> changes;

 private:
  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override {
    FXL_DCHECK(result_state == ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes.emplace_back(std::move(page_change), std::move(callback));
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  fit::closure change_callback_;
};

TEST_P(PageWatcherIntegrationTest, PageWatcherConcurrentTransaction) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  WaitingWatcher watcher(watcher_ptr.NewRequest(),
                         watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                       std::move(watcher_ptr));
  page->PutNew(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes.size());

  page->PutNew(convert::ToArray("foo"), convert::ToArray("bar"));
  page->StartTransactionNew();
  auto transaction_waiter = NewWaiter();
  page->Sync(transaction_waiter->GetCallback());

  RunLoopFor(zx::msec(100));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(1u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[0].callback(nullptr);

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  RunLoopFor(zx::msec(100));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[1].callback(nullptr);

  ASSERT_TRUE(transaction_waiter->RunUntilCalled());
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefix) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), convert::ToArray("01"),
                       std::move(watcher_ptr));
  page->StartTransactionNew();
  page->PutNew(convert::ToArray("00-key"), convert::ToArray("value-00"));
  page->PutNew(convert::ToArray("01-key"), convert::ToArray("value-01"));
  page->PutNew(convert::ToArray("02-key"), convert::ToArray("value-02"));
  page->CommitNew();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChange change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries.size());
  EXPECT_EQ("01-key", convert::ToString(change.changed_entries.at(0).key));
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefixNoChange) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshotNew(snapshot.NewRequest(), convert::ToArray("01"),
                       std::move(watcher_ptr));
  page->PutNew(convert::ToArray("00-key"), convert::ToArray("value-00"));
  page->StartTransactionNew();

  auto waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Starting a transaction drains all watcher notifications, so if we were to
  // be called, we would know at this point.
  EXPECT_EQ(0u, watcher.changes_seen);
}

TEST_P(PageWatcherIntegrationTest, NoChangeTransactionForwardState) {
  auto instance = NewLedgerAppInstance();

  PagePtr page1 = instance->GetTestPage();

  auto waiter =  NewWaiter();
  PageId page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->StartTransactionNew();

  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher_ptr));
  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  auto page2 = instance->GetPage(fidl::MakeOptional(page_id));
  page2->PutNew(convert::ToArray("00-key"), convert::ToArray("value-00"));
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Commit the transaction, and immediately starts another one before letting
  // the Ledger code run anything. The commit should be enough to allow the new
  // state from |page2| to propagate to |page1| given that the transaction is a
  // no-op.
  page1->CommitNew();
  page1->StartTransactionNew();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  page1->RollbackNew();
}

TEST_P(PageWatcherIntegrationTest, RollbackTransactionForwardState) {
  auto instance = NewLedgerAppInstance();

  PagePtr page1 = instance->GetTestPage();

  auto waiter =  NewWaiter();
  PageId page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->StartTransactionNew();

  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher_ptr));
  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  auto page2 = instance->GetPage(fidl::MakeOptional(page_id));
  page2->PutNew(convert::ToArray("00-key"), convert::ToArray("value-00"));
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Rollback the transaction, and immediately starts another one before letting
  // the Ledger code run anything. The rollback should be enough to allow the
  // new state from |page2| to propagate to |page1| given that the transaction
  // is a no-op.
  page1->RollbackNew();
  page1->StartTransactionNew();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(1u, watcher.changes_seen);
  page1->RollbackNew();
}

INSTANTIATE_TEST_SUITE_P(
    PageWatcherIntegrationTest, PageWatcherIntegrationTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()));

}  // namespace
}  // namespace ledger
