// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/encryption/primitives/rand.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"
#include "peridot/lib/callback/capture.h"
#include "peridot/lib/callback/set_when_called.h"
#include "peridot/lib/callback/waiter.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace ledger {
namespace {

storage::PageId RandomId() {
  std::string result;
  result.resize(kPageIdSize);
  encryption::RandBytes(&result[0], kPageIdSize);
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

class LedgerManagerTest : public gtest::TestWithMessageLoop {
 public:
  LedgerManagerTest() : environment_(message_loop_.task_runner(), nullptr) {}

  // gtest::TestWithMessageLoop:
  void SetUp() override {
    gtest::TestWithMessageLoop::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(message_loop_.task_runner());
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync =
        std::make_unique<FakeLedgerSync>(message_loop_.task_runner());
    sync_ptr = sync.get();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_,
        std::make_unique<encryption::FakeEncryptionService>(
            message_loop_.task_runner()),
        std::move(storage), std::move(sync));
    ledger_manager_->BindLedger(ledger_.NewRequest());
    ledger_manager_->BindLedgerDebug(ledger_debug_.NewRequest());
  }

 protected:
  ledger::Environment environment_;
  FakeLedgerStorage* storage_ptr;
  FakeLedgerSync* sync_ptr;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger_;
  LedgerDebugPtr ledger_debug_;
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());

  PagePtr page;
  storage_ptr->should_get_page_fail = true;
  ledger_->GetPage(nullptr, page.NewRequest(),
                   [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.Unbind();
  storage_ptr->ClearCalls();

  storage_ptr->should_get_page_fail = true;
  ledger_->GetRootPage(page.NewRequest(),
                       [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.Unbind();
  storage_ptr->ClearCalls();

  storage::PageId id = RandomId();
  ledger_->GetPage(convert::ToArray(id), page.NewRequest(),
                   [this](Status) { message_loop_.PostQuitTask(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(id, storage_ptr->get_page_calls[0]);
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  page.Unbind();
  storage_ptr->ClearCalls();

  ledger_->DeletePage(convert::ToArray(id),
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
  ledger_.set_error_handler([this, &ledger_closed] {
    ledger_closed = true;
    message_loop_.PostQuitTask();
  });

  ledger_manager_.reset();
  message_loop_.Run();
  EXPECT_TRUE(ledger_closed);
}

TEST_F(LedgerManagerTest, OnEmptyCalled) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  ledger_.Unbind();
  ledger_debug_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(LedgerManagerTest, CallGetPageTwice) {
  PagePtr page;
  storage::PageId id = RandomId();

  uint8_t calls = 0;
  ledger_->GetPage(convert::ToArray(id), page.NewRequest(),
                   [&calls](Status) { calls++; });
  page.Unbind();
  ledger_->GetPage(convert::ToArray(id), page.NewRequest(),
                   [&calls](Status) { calls++; });
  page.Unbind();
  RunLoopUntilIdle();
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
  bool called;
  // Get the root page.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetRootPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Get a new page with a random id.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetPage(
      convert::ToArray(RandomId()), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Create a new page.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetPage(
      nullptr, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);
}

// Verifies that LedgerDebugImpl proxy vended by LedgerManager works correctly.
TEST_F(LedgerManagerTest, CallGetPagesList) {
  std::vector<PagePtr> pages(3);
  std::vector<storage::PageId> ids;

  for (size_t i = 0; i < pages.size(); ++i)
    ids.push_back(RandomId());

  Status status;

  fidl::Array<fidl::Array<uint8_t>> actual_pages_list;

  EXPECT_EQ(0u, actual_pages_list.size());

  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  for (size_t i = 0; i < pages.size(); ++i) {
    ledger_->GetPage(convert::ToArray(ids[i]), pages[i].NewRequest(),
                     waiter->NewCallback());
  }

  bool called;
  waiter->Finalize(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_debug_->GetPagesList(
      callback::Capture(callback::SetWhenCalled(&called), &actual_pages_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(pages.size(), actual_pages_list.size());

  std::sort(ids.begin(), ids.end());
  for (size_t i = 0; i < ids.size(); i++)
    EXPECT_EQ(ids[i], convert::ToString(actual_pages_list[i]));
}

}  // namespace
}  // namespace ledger
