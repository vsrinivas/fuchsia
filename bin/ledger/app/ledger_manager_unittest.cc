// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/public/ledger_storage.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
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
  explicit FakeLedgerStorage(fxl::RefPtr<fxl::TaskRunner> task_runner)
      : task_runner_(std::move(task_runner)) {}
  ~FakeLedgerStorage() override {}

  void CreatePageStorage(
      storage::PageId page_id,
      std::function<void(storage::Status,
                         std::unique_ptr<storage::PageStorage>)> callback)
      override {
    create_page_calls.push_back(std::move(page_id));
    callback(storage::Status::IO_ERROR, nullptr);
  }

  void GetPageStorage(storage::PageId page_id,
                      std::function<void(storage::Status,
                                         std::unique_ptr<storage::PageStorage>)>
                          callback) override {
    get_page_calls.push_back(page_id);
    task_runner_->PostTask([this, callback, page_id]() mutable {
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
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerStorage);
};

class FakeLedgerSync : public cloud_sync::LedgerSync {
 public:
  explicit FakeLedgerSync(fxl::RefPtr<fxl::TaskRunner> task_runner)
      : called(false), task_runner_(std::move(task_runner)) {}
  ~FakeLedgerSync() override {}

  std::unique_ptr<cloud_sync::PageSyncContext> CreatePageContext(
      storage::PageStorage* /*page_storage*/,
      fxl::Closure /*error_callback*/) override {
    return nullptr;
  }

  bool called;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerSync);
};

class LedgerManagerTest : public test::TestWithMessageLoop {
 public:
  LedgerManagerTest() : environment_(message_loop_.task_runner(), nullptr) {}

  // test::TestWithMessageLoop:
  void SetUp() override {
    test::TestWithMessageLoop::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(message_loop_.task_runner());
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync =
        std::make_unique<FakeLedgerSync>(message_loop_.task_runner());
    sync_ptr = sync.get();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, std::move(storage), std::move(sync));
    ledger_manager_->BindLedger(ledger.NewRequest());
  }

 protected:
  ledger::Environment environment_;
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
  storage_ptr->should_get_page_fail = true;
  ledger->GetPage(nullptr, page.NewRequest(),
                  [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
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
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
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

// Cloud should never be queried.
TEST_F(LedgerManagerTest, GetPageDoNotCallTheCloud) {
  storage_ptr->should_get_page_fail = true;
  Status status;

  PagePtr page;

  // Get the root page.
  storage_ptr->ClearCalls();
  page.reset();
  ledger->GetRootPage(page.NewRequest(),
                      callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Get a new page with a random id.
  storage_ptr->ClearCalls();
  page.reset();
  ledger->GetPage(convert::ToArray(RandomId()), page.NewRequest(),
                  callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Create a new page.
  storage_ptr->ClearCalls();
  page.reset();
  ledger->GetPage(nullptr, page.NewRequest(),
                  callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);
}

}  // namespace
}  // namespace ledger
