// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/sync_tests/lib.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/test/data_generator.h"
#include "apps/ledger/src/test/get_ledger.h"

namespace sync_test {
class PageWatcherImpl : public ledger::PageWatcher {
 public:
  PageWatcherImpl() : binding_(this) {}

  auto NewBinding() { return binding_.NewBinding(); }

  int changes = 0;

  ledger::PageSnapshotPtr current_snapshot;

 private:
  // PageWatcher:
  void OnChange(ledger::PageChangePtr page_change,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override {
    changes++;
    current_snapshot.reset();
    callback(current_snapshot.NewRequest());
  }

  fidl::Binding<ledger::PageWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageWatcherImpl);
};

class SyncWatcherImpl : public ledger::SyncWatcher {
 public:
  SyncWatcherImpl() : binding_(this) {}

  auto NewBinding() { return binding_.NewBinding(); }

  ledger::SyncState download;
  ledger::SyncState upload;

 private:
  // SyncWatcher
  void SyncStateChanged(ledger::SyncState download,
                        ledger::SyncState upload,
                        const SyncStateChangedCallback& callback) {
    this->download = download;
    this->upload = upload;
    callback();
  }

  fidl::Binding<ledger::SyncWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

class ConvergenceTest : public SyncTestBase,
                        public ::testing::WithParamInterface<int> {
 public:
  ConvergenceTest(){};
  ~ConvergenceTest() override{};

  void SetUp() override {
    SyncTestBase::SetUp();
    num_ledgers_ = GetParam();
    ASSERT_GT(num_ledgers_, 1);

    fidl::Array<uint8_t> page_id;
    for (int i = 0; i < num_ledgers_; i++) {
      ledgers_.push_back(GetLedger(
          "sync", i == 0 ? test::Erase::ERASE_CLOUD : test::Erase::KEEP_DATA));
      pages_.emplace_back();
      ledger::Status status = test::GetPageEnsureInitialized(
          message_loop_, &(ledgers_[i]->ledger),
          // The first ledger gets a random page id, the others use the same id
          // for their pages.
          i == 0 ? nullptr : page_id.Clone(), &pages_[i], &page_id);
      ASSERT_EQ(ledger::Status::OK, status);
    }
  }

  void TearDown() override { SyncTestBase::TearDown(); }

 protected:
  std::unique_ptr<PageWatcherImpl> WatchPageContents(ledger::PagePtr* page) {
    std::unique_ptr<PageWatcherImpl> watcher =
        std::make_unique<PageWatcherImpl>();
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    (*page)->GetSnapshot(watcher->current_snapshot.NewRequest(), nullptr,
                         watcher->NewBinding(),
                         callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout(ftl::TimeDelta::FromSeconds(10)));
    EXPECT_EQ(ledger::Status::OK, status);
    return watcher;
  }

  std::unique_ptr<SyncWatcherImpl> WatchPageSyncState(ledger::PagePtr* page) {
    std::unique_ptr<SyncWatcherImpl> watcher =
        std::make_unique<SyncWatcherImpl>();
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    (*page)->SetSyncStateWatcher(watcher->NewBinding(),
                                 callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
    return watcher;
  }

  int num_ledgers_;
  std::vector<std::unique_ptr<SyncTestBase::LedgerPtrHolder>> ledgers_;
  std::vector<ledger::PagePtr> pages_;
  test::DataGenerator data_generator_;
};

TEST_P(ConvergenceTest, NLedgersConverge) {
  std::vector<std::unique_ptr<PageWatcherImpl>> watchers;
  std::vector<std::unique_ptr<SyncWatcherImpl>> sync_watchers;
  for (int i = 0; i < num_ledgers_; i++) {
    watchers.push_back(WatchPageContents(&pages_[i]));
    sync_watchers.push_back(WatchPageSyncState(&pages_[i]));

    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    pages_[i]->StartTransaction(callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);

    pages_[i]->Put(convert::ToArray("name"), data_generator_.MakeValue(50),
                   callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
  }

  for (int i = 0; i < num_ledgers_; i++) {
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    pages_[i]->Commit(callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
  }

  std::function<bool()> until = [this, &watchers, &sync_watchers]() {
    // At least one change was propagated, and all synchronization is idle.
    int num_changes = 0;
    for (int i = 0; i < num_ledgers_; i++) {
      num_changes += watchers[i]->changes;
    }
    // All ledgers should see their own change (num_ledgers_). Then, at least
    // all but one should receive a change with the "final" value. There might
    // be more changes seen, though.
    if (num_changes < 2 * num_ledgers_ - 1) {
      return false;
    }

    // All synchronization must be idle.
    for (int i = 0; i < num_ledgers_; i++) {
      if (sync_watchers[i]->download != ledger::SyncState::IDLE ||
          sync_watchers[i]->upload != ledger::SyncState::IDLE) {
        return false;
      }
    }
    return true;
  };

  EXPECT_TRUE(RunLoopUntil(until, ftl::TimeDelta::FromSeconds(60)));

  for (int i = 0; i < num_ledgers_; i++) {
    EXPECT_EQ(ledger::SyncState::IDLE, sync_watchers[i]->download);
    EXPECT_EQ(ledger::SyncState::IDLE, sync_watchers[i]->upload);
  }

  std::vector<fidl::Array<uint8_t>> values;
  for (int i = 0; i < num_ledgers_; i++) {
    values.emplace_back();
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    watchers[i]->current_snapshot->GetInline(
        convert::ToArray("name"),
        callback::Capture(MakeQuitTask(), &status, &values[i]));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
  }

  // We have converged.
  for (int i = 1; i < num_ledgers_; i++) {
    EXPECT_EQ(convert::ToString(values[0]), convert::ToString(values[i]));
  }
}

INSTANTIATE_TEST_CASE_P(ManyLedgersConvergenceTest,
                        ConvergenceTest,
                        ::testing::Range(2, 5));

}  // namespace sync_test
