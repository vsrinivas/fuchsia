// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_page_watcher.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/callback/capture.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

namespace ledger {
namespace {

using ::testing::SizeIs;

class PageWatcherIntegrationTest : public IntegrationTest {
 public:
  PageWatcherIntegrationTest() = default;
  PageWatcherIntegrationTest(const PageWatcherIntegrationTest&) = delete;
  PageWatcherIntegrationTest& operator=(const PageWatcherIntegrationTest&) = delete;
  ~PageWatcherIntegrationTest() override = default;
};

TEST_P(PageWatcherIntegrationTest, PageWatcherSimple) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Alice");
}

TEST_P(PageWatcherIntegrationTest, PageWatcherAggregatedNotifications) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  // Call Put and don't let the OnChange callback be called, yet.
  watcher.DelayCallback(true);
  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->Put(convert::ToArray("key"), convert::ToArray("value1"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  ASSERT_THAT(change->changed_entries, SizeIs(1));
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "key");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "value1");

  // Update the value of "key" initially to "value2" and then to "value3".
  page->Put(convert::ToArray("key"), convert::ToArray("value2"));
  page->Put(convert::ToArray("key"), convert::ToArray("value3"));

  // Since the previous OnChange callback hasn't been called yet, the next
  // notification should be blocked.
  ASSERT_FALSE(watcher_waiter->RunUntilCalled());

  // Call the OnChange callback and expect a new OnChange call.
  watcher.CallOnChangeCallback();
  watcher.DelayCallback(false);
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  // Only the last value of "key" should be found in the changed entries set.
  EXPECT_EQ(watcher.GetChangesSeen(), 2u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  ASSERT_THAT(change->changed_entries, SizeIs(1));
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "key");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "value3");
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectClient) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  auto watcher =
      std::make_unique<TestPageWatcher>(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  // Make a change on the page and verify that it was received.
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher->GetChangesSeen(), 1u);

  // Make another change and disconnect the watcher immediately.
  page->Put(convert::ToArray("name"), convert::ToArray("Bob"));
  watcher.reset();
  auto waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDisconnectPage) {
  auto instance = NewLedgerAppInstance();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  {
    PagePtr page = instance->GetTestPage();
    PageSnapshotPtr snapshot;
    page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

    // Queue many put operations on the page.
    for (int i = 0; i < 1000; i++) {
      page->Put(convert::ToArray("name"), convert::ToArray(std::to_string(i)));
    }
  }
  // Page is out of scope now, but watcher is not. Verify that we don't crash
  // and a change notification is still delivered.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherDelete) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"));

  auto watcher_waiter = NewWaiter();
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  page->Delete(convert::ToArray("foo"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  ASSERT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  EXPECT_EQ(change->changed_entries.size(), 0u);
  ASSERT_EQ(change->deleted_keys.size(), 1u);
  EXPECT_EQ(convert::ToString(change->deleted_keys.at(0)), "foo");
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeSize) {
  auto instance = NewLedgerAppInstance();
  // Put enough entries to ensure we will need more than one query to retrieve
  // them. The number of entries that can be retrieved in one query is bound by
  // |kMaxMessageHandles| and by size of the fidl message (determined by
  // |kMaxInlineDataSize|), so we insert one entry more than that.
  const size_t key_size = kMaxKeySize;
  const size_t entry_size = fidl_serialization::GetEntrySize(key_size);
  const size_t entry_count = std::min(fidl_serialization::kMaxMessageHandles,
                                      fidl_serialization::kMaxInlineDataSize / entry_size) +
                             1;
  const auto key_generator = [](size_t i) {
    std::string prefix = absl::StrFormat("key%03" PRIuMAX, i);
    std::string filler(key_size - prefix.size(), 'k');
    return prefix + filler;
  };
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->StartTransaction();
  for (size_t i = 0; i < entry_count; ++i) {
    page->Put(convert::ToArray(key_generator(i)), convert::ToArray("value"));
  }

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(watcher.GetChangesSeen(), 0u);

  page->Commit();

  // Get the first OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(ResultState::PARTIAL_STARTED, watcher.GetLastResultState());
  auto change = &(watcher.GetLastPageChange());
  size_t initial_size = change->changed_entries.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(convert::ToString(change->changed_entries.at(i).key), key_generator(i));
    EXPECT_EQ(ToString(change->changed_entries.at(i).value), "value");
    EXPECT_EQ(change->changed_entries.at(i).priority, Priority::EAGER);
  }

  // Get the second OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 2u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::PARTIAL_COMPLETED);

  ASSERT_EQ(initial_size + change->changed_entries.size(), entry_count);
  for (size_t i = 0; i < change->changed_entries.size(); ++i) {
    EXPECT_EQ(convert::ToString(change->changed_entries.at(i).key),
              key_generator(i + initial_size));
    EXPECT_EQ(ToString(change->changed_entries.at(i).value), "value");
    EXPECT_EQ(change->changed_entries.at(i).priority, Priority::EAGER);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherBigChangeHandles) {
  auto instance = NewLedgerAppInstance();
  size_t entry_count = 70;
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->StartTransaction();
  for (size_t i = 0; i < entry_count; ++i) {
    page->Put(convert::ToArray(absl::StrFormat("key%02" PRIuMAX, i)), convert::ToArray("value"));
  }

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(watcher.GetChangesSeen(), 0u);

  page->Commit();

  // Get the first OnChagne call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(ResultState::PARTIAL_STARTED, watcher.GetLastResultState());
  auto change = &(watcher.GetLastPageChange());
  size_t initial_size = change->changed_entries.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(convert::ToString(change->changed_entries.at(i).key),
              absl::StrFormat("key%02" PRIuMAX, i));
    EXPECT_EQ(ToString(change->changed_entries.at(i).value), "value");
    EXPECT_EQ(change->changed_entries.at(i).priority, Priority::EAGER);
  }

  // Get the second OnChange call.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 2u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::PARTIAL_COMPLETED);

  ASSERT_EQ(initial_size + change->changed_entries.size(), entry_count);
  for (size_t i = 0; i < change->changed_entries.size(); ++i) {
    EXPECT_EQ(convert::ToString(change->changed_entries.at(i).key),
              absl::StrFormat("key%02" PRIuMAX, i + initial_size));
    EXPECT_EQ(ToString(change->changed_entries.at(i).value), "value");
    EXPECT_EQ(change->changed_entries.at(i).priority, Priority::EAGER);
  }
}

TEST_P(PageWatcherIntegrationTest, PageWatcherSnapshot) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto entries = SnapshotGetEntries(this, watcher.GetLastSnapshot());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(convert::ToString(entries[0].key), "name");
  EXPECT_EQ(ToString(entries[0].value), "Alice");
  EXPECT_EQ(entries[0].priority, Priority::EAGER);
}

TEST_P(PageWatcherIntegrationTest, PageWatcherTransaction) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->StartTransaction();
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(watcher.GetChangesSeen(), 0u);

  page->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Alice");
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
  TestPageWatcher watcher1(watcher1_ptr.NewRequest(), watcher_waiter1->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  auto watcher_waiter2 = NewWaiter();
  TestPageWatcher watcher2(watcher2_ptr.NewRequest(), watcher_waiter2->GetCallback());
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));
  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));

  // Verify that each change is seen by the right watcher.
  page1->Commit();

  ASSERT_TRUE(watcher_waiter1->RunUntilCalled());
  EXPECT_EQ(watcher1.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher1.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher1.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Alice");

  page2->Commit();

  ASSERT_TRUE(watcher_waiter2->RunUntilCalled());
  EXPECT_EQ(watcher2.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher2.GetLastResultState(), ResultState::COMPLETED);
  change = &(watcher2.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Bob");

  RunLoopFor(zx::msec(100));

  // A merge happens now. Only the first watcher should see a change.
  ASSERT_TRUE(watcher_waiter1->RunUntilCalled());
  EXPECT_EQ(watcher1.GetChangesSeen(), 2u);
  EXPECT_EQ(watcher2.GetLastResultState(), ResultState::COMPLETED);
  EXPECT_EQ(watcher2.GetChangesSeen(), 1u);

  change = &(watcher1.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Bob");
}

TEST_P(PageWatcherIntegrationTest, PageWatcherEmptyTransaction) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->StartTransaction();
  page->Commit();

  RunLoopFor(zx::msec(100));
  EXPECT_EQ(watcher.GetChangesSeen(), 0u);
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
  TestPageWatcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  auto watcher2_waiter = NewWaiter();
  PageWatcherPtr watcher2_ptr;
  TestPageWatcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());

  ASSERT_EQ(watcher1.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher1.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher1.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Alice");

  ASSERT_EQ(watcher2.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher2.GetLastResultState(), ResultState::COMPLETED);
  change = &(watcher2.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change->changed_entries.at(0).value), "Alice");
}

class WaitingWatcher : public PageWatcher {
 public:
  WaitingWatcher(fidl::InterfaceRequest<PageWatcher> request, fit::closure change_callback)
      : binding_(this, std::move(request)), change_callback_(std::move(change_callback)) {}

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
  WaitingWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.changes.size(), 1u);

  page->Put(convert::ToArray("foo"), convert::ToArray("bar"));
  page->StartTransaction();
  auto transaction_waiter = NewWaiter();
  page->Sync(transaction_waiter->GetCallback());

  RunLoopFor(zx::msec(100));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(watcher.changes.size(), 1u);
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[0].callback(nullptr);

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.changes.size(), 2u);
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  RunLoopFor(zx::msec(100));

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(watcher.changes.size(), 2u);
  EXPECT_TRUE(transaction_waiter->NotCalledYet());

  watcher.changes[1].callback(nullptr);

  ASSERT_TRUE(transaction_waiter->RunUntilCalled());
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefix) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"), std::move(watcher_ptr));
  page->StartTransaction();
  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"));
  page->Put(convert::ToArray("01-key"), convert::ToArray("value-01"));
  page->Put(convert::ToArray("02-key"), convert::ToArray("value-02"));
  page->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  EXPECT_EQ(watcher.GetLastResultState(), ResultState::COMPLETED);
  auto change = &(watcher.GetLastPageChange());
  ASSERT_EQ(change->changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change->changed_entries.at(0).key), "01-key");
}

TEST_P(PageWatcherIntegrationTest, PageWatcherPrefixNoChange) {
  auto instance = NewLedgerAppInstance();
  PagePtr page = instance->GetTestPage();
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"), std::move(watcher_ptr));
  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"));
  page->StartTransaction();

  auto waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Starting a transaction drains all watcher notifications, so if we were to
  // be called, we would know at this point.
  EXPECT_EQ(watcher.GetChangesSeen(), 0u);
}

TEST_P(PageWatcherIntegrationTest, NoChangeTransactionForwardState) {
  auto instance = NewLedgerAppInstance();

  PagePtr page1 = instance->GetTestPage();

  auto waiter = NewWaiter();
  PageId page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->StartTransaction();

  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  auto page2 = instance->GetPage(fidl::MakeOptional(page_id));
  page2->Put(convert::ToArray("00-key"), convert::ToArray("value-00"));
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Commit the transaction, and immediately starts another one before letting
  // the Ledger code run anything. The commit should be enough to allow the new
  // state from |page2| to propagate to |page1| given that the transaction is a
  // no-op.
  page1->Commit();
  page1->StartTransaction();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  page1->Rollback();
}

TEST_P(PageWatcherIntegrationTest, RollbackTransactionForwardState) {
  auto instance = NewLedgerAppInstance();

  PagePtr page1 = instance->GetTestPage();

  auto waiter = NewWaiter();
  PageId page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->StartTransaction();

  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  TestPageWatcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));
  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  auto page2 = instance->GetPage(fidl::MakeOptional(page_id));
  page2->Put(convert::ToArray("00-key"), convert::ToArray("value-00"));
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Rollback the transaction, and immediately starts another one before letting
  // the Ledger code run anything. The rollback should be enough to allow the
  // new state from |page2| to propagate to |page1| given that the transaction
  // is a no-op.
  page1->Rollback();
  page1->StartTransaction();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.GetChangesSeen(), 1u);
  page1->Rollback();
}

INSTANTIATE_TEST_SUITE_P(PageWatcherIntegrationTest, PageWatcherIntegrationTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
