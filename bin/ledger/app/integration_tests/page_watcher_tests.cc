// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/fidl/serialization_size.h"
#include "apps/ledger/src/app/integration_tests/integration_test.h"
#include "apps/ledger/src/app/integration_tests/test_utils.h"
#include "apps/ledger/src/convert/convert.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace integration_tests {
namespace {

class PageWatcherIntegrationTest : public IntegrationTest {
 public:
  PageWatcherIntegrationTest() {}
  ~PageWatcherIntegrationTest() override {}

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageWatcherIntegrationTest);
};

class Watcher : public PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request,
          ftl::Closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  uint changes_seen = 0;
  ResultState last_result_state_;
  PageSnapshotPtr last_snapshot_;
  PageChangePtr last_page_change_;

 private:
  // PageWatcher:
  void OnChange(PageChangePtr page_change,
                ResultState result_state,
                const OnChangeCallback& callback) override {
    FTL_DCHECK(page_change);
    changes_seen++;
    last_result_state_ = result_state;
    last_page_change_ = std::move(page_change);
    last_snapshot_.reset();
    callback(last_snapshot_.NewRequest());
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  ftl::Closure change_callback_;
};

TEST_F(PageWatcherIntegrationTest, PageWatcherSimple) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChangePtr change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice", ToString(change->changes[0]->value));
}

TEST_F(PageWatcherIntegrationTest, PageWatcherDelete) {
  PagePtr page = GetTestPage();
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Delete(convert::ToArray("foo"),
               [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChangePtr change = std::move(watcher.last_page_change_);
  EXPECT_EQ(0u, change->changes.size());
  ASSERT_EQ(1u, change->deleted_keys.size());
  EXPECT_EQ("foo", convert::ToString(change->deleted_keys[0]));
}

TEST_F(PageWatcherIntegrationTest, PageWatcherBigChangeSize) {
  const size_t entry_count = 2;
  const auto key_generator = [](size_t i) {
    std::string filler(
        fidl_serialization::kMaxInlineDataSize * 3 / 2 / entry_count, 'k');
    return ftl::StringPrintf("key%02" PRIuMAX "%s", i, filler.c_str());
  };
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  for (size_t i = 0; i < entry_count; ++i) {
    page->Put(convert::ToArray(key_generator(i)), convert::ToArray("value"),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }

  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(100)));
  EXPECT_EQ(0u, watcher.changes_seen);

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  // Get the first OnChagne call.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ResultState::PARTIAL_STARTED);
  PageChangePtr change = std::move(watcher.last_page_change_);
  size_t initial_size = change->changes.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(key_generator(i), convert::ToString(change->changes[i]->key));
    EXPECT_EQ("value", ToString(change->changes[i]->value));
    EXPECT_EQ(Priority::EAGER, change->changes[i]->priority);
  }

  // Get the second OnChagne call.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change->changes.size());
  for (size_t i = 0; i < change->changes.size(); ++i) {
    EXPECT_EQ(key_generator(i + initial_size),
              convert::ToString(change->changes[i]->key));
    EXPECT_EQ("value", ToString(change->changes[i]->value));
    EXPECT_EQ(Priority::EAGER, change->changes[i]->priority);
  }
}

TEST_F(PageWatcherIntegrationTest, PageWatcherBigChangeHandles) {
  size_t entry_count = 70;
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  for (size_t i = 0; i < entry_count; ++i) {
    page->Put(convert::ToArray(ftl::StringPrintf("key%02" PRIuMAX, i)),
              convert::ToArray("value"),
              [](Status status) { EXPECT_EQ(status, Status::OK); });
    EXPECT_TRUE(page.WaitForIncomingResponse());
  }

  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(100)));
  EXPECT_EQ(0u, watcher.changes_seen);

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  // Get the first OnChagne call.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(watcher.last_result_state_, ResultState::PARTIAL_STARTED);
  PageChangePtr change = std::move(watcher.last_page_change_);
  size_t initial_size = change->changes.size();
  for (size_t i = 0; i < initial_size; ++i) {
    EXPECT_EQ(ftl::StringPrintf("key%02" PRIuMAX, i),
              convert::ToString(change->changes[i]->key));
    EXPECT_EQ("value", ToString(change->changes[i]->value));
    EXPECT_EQ(Priority::EAGER, change->changes[i]->priority);
  }

  // Get the second OnChagne call.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, watcher.changes_seen);
  EXPECT_EQ(ResultState::PARTIAL_COMPLETED, watcher.last_result_state_);
  change = std::move(watcher.last_page_change_);

  ASSERT_EQ(entry_count, initial_size + change->changes.size());
  for (size_t i = 0; i < change->changes.size(); ++i) {
    EXPECT_EQ(ftl::StringPrintf("key%02" PRIuMAX, i + initial_size),
              convert::ToString(change->changes[i]->key));
    EXPECT_EQ("value", ToString(change->changes[i]->value));
    EXPECT_EQ(Priority::EAGER, change->changes[i]->priority);
  }
}

TEST_F(PageWatcherIntegrationTest, PageWatcherSnapshot) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  fidl::Array<EntryPtr> entries =
      SnapshotGetEntries(&(watcher.last_snapshot_), convert::ToArray(""));
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ("name", convert::ToString(entries[0]->key));
  EXPECT_EQ("Alice", ToString(entries[0]->value));
  EXPECT_EQ(Priority::EAGER, entries[0]->priority);
}

TEST_F(PageWatcherIntegrationTest, PageWatcherTransaction) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_EQ(0u, watcher.changes_seen);

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChangePtr change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice", ToString(change->changes[0]->value));
}

TEST_F(PageWatcherIntegrationTest, PageWatcherParallel) {
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(watcher1_ptr.NewRequest(),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();
  EXPECT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher1.last_result_state_);
  PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice", ToString(change->changes[0]->value));

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  mtl::MessageLoop::GetCurrent()->Run();

  EXPECT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); },
      ftl::TimeDelta::FromSeconds(1));
  mtl::MessageLoop::GetCurrent()->Run();
  // A merge happens now. Only the first watcher should see a change.
  EXPECT_EQ(2u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  EXPECT_EQ(1u, watcher2.changes_seen);

  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));
}

TEST_F(PageWatcherIntegrationTest, PageWatcherEmptyTransaction) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_EQ(0u, watcher.changes_seen);
}

TEST_F(PageWatcherIntegrationTest, PageWatcher1Change2Pages) {
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(watcher1_ptr.NewRequest(),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(1u, watcher1.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher1.last_result_state_);
  PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice", ToString(change->changes[0]->value));

  ASSERT_EQ(1u, watcher2.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher2.last_result_state_);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Alice", ToString(change->changes[0]->value));
}

class WaitingWatcher : public PageWatcher {
 public:
  WaitingWatcher(fidl::InterfaceRequest<PageWatcher> request,
                 ftl::Closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  struct Change {
    PageChangePtr change;
    OnChangeCallback callback;

    Change(PageChangePtr change, OnChangeCallback callback)
        : change(std::move(change)), callback(std::move(callback)) {}
  };

  std::vector<Change> changes;

 private:
  // PageWatcher:
  void OnChange(PageChangePtr page_change,
                ResultState result_state,
                const OnChangeCallback& callback) override {
    FTL_DCHECK(page_change);
    FTL_DCHECK(result_state == ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes.emplace_back(std::move(page_change), callback);
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  ftl::Closure change_callback_;
};

TEST_F(PageWatcherIntegrationTest, PageWatcherConcurrentTransaction) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  WaitingWatcher watcher(watcher_ptr.NewRequest(), []() {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                    [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("name"), convert::ToArray("Alice"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes.size());

  page->Put(convert::ToArray("foo"), convert::ToArray("bar"),
            [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page.WaitForIncomingResponse());

  bool start_transaction_callback_called = false;
  Status start_transaction_status;
  page->StartTransaction([&start_transaction_callback_called,
                          &start_transaction_status](Status status) {
    start_transaction_callback_called = true;
    start_transaction_status = status;
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  EXPECT_TRUE(RunLoopWithTimeout());

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(1u, watcher.changes.size());
  EXPECT_FALSE(start_transaction_callback_called);

  watcher.changes[0].callback(nullptr);

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_FALSE(start_transaction_callback_called);

  EXPECT_TRUE(RunLoopWithTimeout());

  // We haven't sent the callback of the first change, so nothing should have
  // happened.
  EXPECT_EQ(2u, watcher.changes.size());
  EXPECT_FALSE(start_transaction_callback_called);

  watcher.changes[1].callback(nullptr);

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(start_transaction_callback_called);
  EXPECT_EQ(Status::OK, start_transaction_status);
}

TEST_F(PageWatcherIntegrationTest, PageWatcherPrefix) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  auto callback_statusok = [](Status status) { EXPECT_EQ(Status::OK, status); };
  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"),
                    std::move(watcher_ptr), callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction(callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"),
            callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Put(convert::ToArray("01-key"), convert::ToArray("value-01"),
            callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Put(convert::ToArray("02-key"), convert::ToArray("value-02"),
            callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());
  page->Commit(callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher.changes_seen);
  EXPECT_EQ(ResultState::COMPLETED, watcher.last_result_state_);
  PageChangePtr change = std::move(watcher.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("01-key", convert::ToString(change->changes[0]->key));
}

TEST_F(PageWatcherIntegrationTest, PageWatcherPrefixNoChange) {
  PagePtr page = GetTestPage();
  PageWatcherPtr watcher_ptr;
  Watcher watcher(watcher_ptr.NewRequest(),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });

  auto callback_statusok = [](Status status) { EXPECT_EQ(Status::OK, status); };
  PageSnapshotPtr snapshot;
  page->GetSnapshot(snapshot.NewRequest(), convert::ToArray("01"),
                    std::move(watcher_ptr), callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->Put(convert::ToArray("00-key"), convert::ToArray("value-00"),
            callback_statusok);
  EXPECT_TRUE(page.WaitForIncomingResponse());

  page->StartTransaction([](Status status) {
    EXPECT_EQ(Status::OK, status);
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  // Starting a transaction drains all watcher notifications, so if we were to
  // be called, we would know at this point.
  EXPECT_EQ(0u, watcher.changes_seen);
}

}  // namespace
}  // namespace integration_tests
}  // namespace ledger
