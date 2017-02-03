// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_manager.h"

#include <memory>
#include <vector>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/public/ledger_storage.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

storage::PageId RandomId() {
  std::string result;
  result.resize(kPageIdSize);
  glue::RandBytes(&result[0], kPageIdSize);
  return result;
}

class FakeLedgerStorage : public storage::LedgerStorage {
 public:
  FakeLedgerStorage(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(task_runner) {}
  ~FakeLedgerStorage() {}

  storage::Status CreatePageStorage(
      storage::PageId page_id,
      std::unique_ptr<storage::PageStorage>* page_storage) override {
    create_page_calls.push_back(std::move(page_id));
    page_storage->reset();
    return storage::Status::IO_ERROR;
  }

  void GetPageStorage(
      storage::PageId page_id,
      const std::function<void(storage::Status,
                               std::unique_ptr<storage::PageStorage>)>&
          callback) override {
    get_page_calls.push_back(page_id);
    task_runner_->PostTask([this, callback, page_id]() {
      if (should_get_page_fail) {
        callback(storage::Status::NOT_FOUND, nullptr);
      } else {
        callback(storage::Status::OK,
                 std::make_unique<storage::fake::FakePageStorage>(
                     std::move(page_id)));
      }
    });
  }

  bool DeletePageStorage(storage::PageIdView page_id) override {
    delete_page_calls.push_back(page_id.ToString());
    return false;
  }

  void ClearCalls() {
    create_page_calls.clear();
    get_page_calls.clear();
    delete_page_calls.clear();
  }

  bool should_get_page_fail = false;
  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;
  std::vector<storage::PageId> delete_page_calls;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerStorage);
};

class FakeLedgerSync : public cloud_sync::LedgerSync {
 public:
  FakeLedgerSync(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(task_runner) {}
  ~FakeLedgerSync() {}

  void RemoteContains(
      ftl::StringView page_id,
      std::function<void(cloud_sync::RemoteResponse)> callback) override {
    remote_contains_calls++;
    task_runner_->PostTask([ this, callback = std::move(callback) ] {
      callback(response_to_return);
    });
  }

  std::unique_ptr<cloud_sync::PageSyncContext> CreatePageContext(
      storage::PageStorage* page_storage,
      ftl::Closure error_callback) override {
    return nullptr;
  }

  cloud_sync::RemoteResponse response_to_return =
      cloud_sync::RemoteResponse::NOT_FOUND;

  int remote_contains_calls = 0;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerSync);
};

class LedgerManagerTest : public test::TestWithMessageLoop {
 public:
  LedgerManagerTest() {}

  // test::TestWithMessageLoop:
  void SetUp() override {
    test::TestWithMessageLoop::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(message_loop_.task_runner());
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync =
        std::make_unique<FakeLedgerSync>(message_loop_.task_runner());
    sync_ptr = sync.get();
    ledger_manager_ =
        std::make_unique<LedgerManager>(std::move(storage), std::move(sync));
    ledger_manager_->BindLedger(ledger.NewRequest());
  }

 protected:
  FakeLedgerStorage* storage_ptr;
  FakeLedgerSync* sync_ptr;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger;
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());

  PagePtr page;
  ledger->NewPage(page.NewRequest(),
                  [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.reset();
  storage_ptr->ClearCalls();

  storage_ptr->should_get_page_fail = true;
  ledger->GetRootPage(page.NewRequest(),
                      [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.reset();
  storage_ptr->ClearCalls();

  storage::PageId id = RandomId();
  ledger->GetPage(convert::ToArray(id), page.NewRequest(),
                  [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(id, storage_ptr->get_page_calls[0]);
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.reset();
  storage_ptr->ClearCalls();

  ledger->DeletePage(convert::ToArray(id),
                     [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->delete_page_calls.size());
  EXPECT_EQ(id, storage_ptr->delete_page_calls[0]);
  storage_ptr->ClearCalls();
}

// Verifies that deleting the LedgerManager closes the channels connected to
// LedgerImpl.
TEST_F(LedgerManagerTest, DeletingLedgerManagerClosesConnections) {
  bool ledger_closed = false;
  ledger.set_connection_error_handler([this, &ledger_closed] {
    ledger_closed = true;
    message_loop_.PostQuitTask();
  });

  ledger_manager_.reset();
  message_loop_.Run();
  EXPECT_TRUE(ledger_closed);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(LedgerManagerTest, CallGetPageTwice) {
  PagePtr page;
  storage::PageId id = RandomId();

  uint8_t calls = 0;
  ledger->GetPage(convert::ToArray(id), page.NewRequest(),
                  [this, &calls](Status) {
                    calls++;
                    message_loop_.PostQuitTask();
                  });
  page.reset();
  ledger->GetPage(convert::ToArray(id), page.NewRequest(),
                  [this, &calls](Status) {
                    calls++;
                    message_loop_.PostQuitTask();
                  });
  page.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, calls);
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(id, storage_ptr->get_page_calls[0]);
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
}

// Cloud shouldn't be queried if the page is present locally.
TEST_F(LedgerManagerTest, GetPageFromStorageDontAskTheCloud) {
  storage_ptr->should_get_page_fail = false;
  Status status;

  // Get the page when present in storage but not in the cloud.
  sync_ptr->response_to_return = cloud_sync::RemoteResponse::NOT_FOUND;
  PagePtr page1;
  ledger->GetPage(
      convert::ToArray(RandomId()), page1.NewRequest(),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(0, sync_ptr->remote_contains_calls);

  // Get the page when present in storage and in the cloud.
  sync_ptr->response_to_return = cloud_sync::RemoteResponse::FOUND;
  PagePtr page2;
  ledger->GetPage(
      convert::ToArray(RandomId()), page2.NewRequest(),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(0, sync_ptr->remote_contains_calls);
}

TEST_F(LedgerManagerTest, GetPageFromTheCloud) {
  storage_ptr->should_get_page_fail = true;
  Status status;

  // Get the page when not present in the cloud.
  sync_ptr->response_to_return = cloud_sync::RemoteResponse::NOT_FOUND;
  PagePtr page1;
  ledger->GetPage(
      convert::ToArray(RandomId()), page1.NewRequest(),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
  EXPECT_EQ(1, sync_ptr->remote_contains_calls);

  // Get the page when we can't reach the cloud.
  sync_ptr->response_to_return = cloud_sync::RemoteResponse::NETWORK_ERROR;
  PagePtr page2;
  ledger->GetPage(
      convert::ToArray(RandomId()), page2.NewRequest(),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  // Gotcha: this returns INTERNAL_ERROR here, because we end up trying to
  // create the local page storage, and FakeLedgerStorage above can't do that
  // and returns IO_ERROR.
  // TODO(ppi): make FakeLedgerStorage more capable and expect OK below.
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_EQ(2, sync_ptr->remote_contains_calls);

  // Get the page when present in the cloud.
  sync_ptr->response_to_return = cloud_sync::RemoteResponse::FOUND;
  PagePtr page3;
  ledger->GetPage(
      convert::ToArray(RandomId()), page3.NewRequest(),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  // Gotcha: this returns INTERNAL_ERROR here, because we end up trying to
  // create the local page storage, and FakeLedgerStorage above can't do that
  // and returns IO_ERROR.
  // TODO(ppi): make FakeLedgerStorage more capable and expect OK below.
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_EQ(3, sync_ptr->remote_contains_calls);
}

}  // namespace
}  // namespace ledger
