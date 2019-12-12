// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/ledger/bin/app/background_sync_manager.h"

#include <unistd.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

namespace {

// A Delegate that helps to keep track of pages being propagated for synchronization with the
// cloud.
class FakeDelegate : public BackgroundSyncManager::Delegate {
 public:
  FakeDelegate(Environment* environment, PageUsageDb* db,
               BackgroundSyncManager* background_sync_manager)
      : db_(db),
        background_sync_manager_(background_sync_manager),
        coroutine_manager_(environment->coroutine_service()) {}

  ~FakeDelegate() override = default;

  void TrySyncClosedPage(absl::string_view ledger_name, storage::PageIdView page_id) override {
    ++sync_calls_[{convert::ToString(ledger_name), convert::ToString(page_id)}];
    coroutine_manager_.StartCoroutine(
        [db = db_, background_sync_manager = background_sync_manager_,
         ledger_name_str = convert::ToString(ledger_name),
         page_id_str = convert::ToString(page_id)](coroutine::CoroutineHandler* handler) {
          Status status = db->MarkPageOpened(handler, ledger_name_str, page_id_str);
          EXPECT_EQ(status, Status::OK);
          if (status != Status::OK) {
            return;
          }
          background_sync_manager->OnInternallyUsed(ledger_name_str, page_id_str);
        });
  }

  // Simulates the end of page sync by marking it as closed and sending notifications about page
  // being closed.
  void FinishSyncPage(absl::string_view ledger_name, storage::PageIdView page_id) {
    coroutine_manager_.StartCoroutine(
        [db = db_, background_sync_manager = background_sync_manager_,
         ledger_name_str = convert::ToString(ledger_name),
         page_id_str = convert::ToString(page_id)](coroutine::CoroutineHandler* handler) {
          Status status = db->MarkPageClosed(handler, ledger_name_str, page_id_str);
          EXPECT_EQ(status, Status::OK);
          if (status != Status::OK) {
            return;
          }
          background_sync_manager->OnInternallyUnused(ledger_name_str, page_id_str);
        });
  }

  // Returns the number of times synchronization was triggered for the given page.
  int GetSyncCallsCount(const std::string& ledger_name, const storage::PageId& page_id) {
    auto page_it = sync_calls_.find({ledger_name, page_id});
    if (page_it == sync_calls_.end()) {
      return 0;
    }
    return page_it->second;
  }

  // Removes all the information about previous calls for pages sync.
  void Clear() { sync_calls_.clear(); }

 private:
  // Stores a counter per page that records how many times the sync with the cloud was triggered.
  std::map<std::pair<std::string, storage::PageId>, int> sync_calls_;
  PageUsageDb* db_;
  BackgroundSyncManager* background_sync_manager_;
  coroutine::CoroutineManager coroutine_manager_;
};

}  // namespace

class BackgroundSyncManagerTest : public TestWithEnvironment {
 public:
  BackgroundSyncManagerTest()
      : tmp_location_(environment_.file_system()->CreateScopedTmpLocation()),
        db_factory_(environment_.file_system(), environment_.dispatcher()) {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    ResetPageUsageDb();
    background_sync_manager_ =
        std::make_unique<BackgroundSyncManager>(&environment_, db_.get(), /*open_pages_limit=*/10);
    delegate_ =
        std::make_unique<FakeDelegate>(&environment_, db_.get(), background_sync_manager_.get());
    background_sync_manager_->SetDelegate(delegate_.get());
  }

  void ResetPageUsageDb() {
    EXPECT_TRUE(RunInCoroutine([this](coroutine::CoroutineHandler* handler) {
      Status status;
      std::unique_ptr<storage::Db> leveldb;
      if (coroutine::SyncCall(
              handler,
              [this](fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) mutable {
                db_factory_.GetOrCreateDb(tmp_location_->path(),
                                          storage::DbFactory::OnDbNotFound::CREATE,
                                          std::move(callback));
              },
              &status, &leveldb) == coroutine::ContinuationStatus::INTERRUPTED) {
        FAIL();
        return;
      }
      EXPECT_EQ(status, Status::OK);
      dbview_factory_ = std::make_unique<DbViewFactory>(std::move(leveldb));
      db_ = std::make_unique<PageUsageDb>(
          &environment_, dbview_factory_->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));
    }));
  }

 private:
  std::unique_ptr<ScopedTmpLocation> tmp_location_;

 protected:
  storage::fake::FakeDbFactory db_factory_;
  std::unique_ptr<DbViewFactory> dbview_factory_;
  std::unique_ptr<PageUsageDb> db_;
  std::unique_ptr<BackgroundSyncManager> background_sync_manager_;
  std::unique_ptr<FakeDelegate> delegate_;
};

TEST_F(BackgroundSyncManagerTest, AsynchronousExternalUsedAndUnused) {
  std::string ledger_name = "ledger";
  storage::PageId first_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // Check for external usage.
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUsed(ledger_name, first_page_id);
    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUsed(ledger_name, first_page_id);
    background_sync_manager_->OnExternallyUnused(ledger_name, first_page_id);

    RunLoopUntilIdle();
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 0);

    background_sync_manager_->OnExternallyUnused(ledger_name, first_page_id);

    RunLoopUntilIdle();
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 1);
  });
}

TEST_F(BackgroundSyncManagerTest, AsynchronousInternalUsedAndUnused) {
  std::string ledger_name = "ledger";
  storage::PageId first_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // Check for internal usage.
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnInternallyUsed(ledger_name, first_page_id);
    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnInternallyUsed(ledger_name, first_page_id);
    background_sync_manager_->OnInternallyUnused(ledger_name, first_page_id);

    RunLoopUntilIdle();
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 0);

    background_sync_manager_->OnInternallyUnused(ledger_name, first_page_id);

    RunLoopUntilIdle();
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 1);
  });
}

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35727): This test should be rewritten,
// once there is any mechanism to store a synchronization state of a page. In this case, sync should
// not be triggered for the page again, if it has just been closed after sync.
TEST_F(BackgroundSyncManagerTest, SyncOnPageUnused) {
  std::string ledger_name = "ledger";
  storage::PageId first_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');
  storage::PageId second_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '2');

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUsed(ledger_name, first_page_id);

    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUnused(ledger_name, first_page_id);
    RunLoopUntilIdle();

    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 1);
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, second_page_id), 0);
    delegate_->FinishSyncPage(ledger_name, first_page_id);
    RunLoopUntilIdle();

    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, second_page_id), Status::OK);
    background_sync_manager_->OnInternallyUsed(ledger_name, second_page_id);

    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, second_page_id), Status::OK);
    background_sync_manager_->OnInternallyUnused(ledger_name, second_page_id);
    RunLoopUntilIdle();

    // By the time the second page is unused, first one is closed after synchronization. This leads
    // to sync being triggered for this page again, as the limit of open at once pages was not
    // reached.
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 2);
    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, second_page_id), 1);
  });
}

TEST_F(BackgroundSyncManagerTest, DontSyncWhenInternalConnectionRemain) {
  std::string ledger_name = "ledger";
  storage::PageId first_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUsed(ledger_name, first_page_id);
    background_sync_manager_->OnInternallyUsed(ledger_name, first_page_id);

    background_sync_manager_->OnExternallyUnused(ledger_name, first_page_id);
    RunLoopUntilIdle();

    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 0);
  });
}

TEST_F(BackgroundSyncManagerTest, DontSyncWhenExternalConnectionRemain) {
  std::string ledger_name = "ledger";
  storage::PageId first_page_id = std::string(::fuchsia::ledger::PAGE_ID_SIZE, '1');

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, first_page_id), Status::OK);
    background_sync_manager_->OnExternallyUsed(ledger_name, first_page_id);
    background_sync_manager_->OnInternallyUsed(ledger_name, first_page_id);

    background_sync_manager_->OnInternallyUnused(ledger_name, first_page_id);
    RunLoopUntilIdle();

    EXPECT_EQ(delegate_->GetSyncCallsCount(ledger_name, first_page_id), 0);
  });
}

// Verifies that BackgroundSyncManager does not exceed the limit of allowed number of open at once
// pages by starting synchronizartion of closed pages.
TEST_F(BackgroundSyncManagerTest, DontStartSyncWhenManyPageAreOpen) {
  // Sets the limit of allowed number of open pages to 2.
  BackgroundSyncManager background_sync_manager =
      BackgroundSyncManager(&environment_, db_.get(), 2);
  FakeDelegate delegate = FakeDelegate(&environment_, db_.get(), &background_sync_manager);
  background_sync_manager.SetDelegate(&delegate);

  std::string ledger_name = "ledger";
  std::vector<storage::PageId> page_ids;
  for (int id = 0; id < 4; ++id) {
    page_ids.push_back(storage::PageId(std::string(::fuchsia::ledger::PAGE_ID_SIZE, '0' + id)));
  }

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    for (size_t id = 0; id < 3; ++id) {
      // Marks the given page as closed in the Db and waits for the operation to be finished to
      // guarantee the preservation of the call order among corresponding closure timestamps.
      EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, page_ids[id]), Status::OK);
      RunLoopUntilIdle();
    }

    EXPECT_EQ(db_->MarkPageOpened(handler, ledger_name, page_ids[3]), Status::OK);
    background_sync_manager.OnExternallyUsed(ledger_name, page_ids[3]);

    EXPECT_EQ(db_->MarkPageClosed(handler, ledger_name, page_ids[3]), Status::OK);
    background_sync_manager.OnExternallyUnused(ledger_name, page_ids[3]);
    RunLoopUntilIdle();

    // The closure of fourth page should trigger sync for the first and second ones since they are
    // marked as closed and have the oldest timestamps.
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[0]), 1);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[1]), 1);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[2]), 0);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[3]), 0);

    delegate.FinishSyncPage(ledger_name, page_ids[0]);
    RunLoopUntilIdle();

    // The end of first page synchronization should trigger the sync of third one, since the page is
    // marked as closed after sync is finished.
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[0]), 1);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[1]), 1);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[2]), 1);
    EXPECT_EQ(delegate.GetSyncCallsCount(ledger_name, page_ids[3]), 0);
  });
}

}  // namespace ledger
