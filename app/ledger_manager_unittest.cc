// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_manager.h"

#include <memory>
#include <vector>

#include "apps/ledger/app/constants.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/public/ledger_storage.h"
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
  FakeLedgerStorage() {}
  ~FakeLedgerStorage() {}

  storage::Status CreatePageStorage(
      storage::PageIdView page_id,
      std::unique_ptr<storage::PageStorage>* page_storage) override {
    create_page_calls.push_back(page_id.ToString());
    page_storage->reset();
    return storage::Status::IO_ERROR;
  }

  void GetPageStorage(
      storage::PageIdView page_id,
      const std::function<void(std::unique_ptr<storage::PageStorage>)>&
          callback) override {
    get_page_calls.push_back(page_id.ToString());
    callback(nullptr);
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

  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;
  std::vector<storage::PageId> delete_page_calls;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerStorage);
};

class LedgerManagerTest : public ::testing::Test {
 public:
  LedgerManagerTest() {}
  ~LedgerManagerTest() override {}

 protected:
  mtl::MessageLoop message_loop_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerManagerTest);
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  std::unique_ptr<FakeLedgerStorage> storage =
      std::make_unique<FakeLedgerStorage>();
  FakeLedgerStorage* storage_ptr = storage.get();
  LedgerManager ledger_manager(std::move(storage));

  LedgerPtr ledger = ledger_manager.GetLedgerPtr();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());

  ledger->NewPage(
      [this](Status, mojo::InterfaceHandle<Page>) { message_loop_.QuitNow(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  storage_ptr->ClearCalls();

  ledger->GetRootPage(
      [this](Status, mojo::InterfaceHandle<Page>) { message_loop_.QuitNow(); });
  message_loop_.Run();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  storage_ptr->ClearCalls();

  storage::PageId id = RandomId();
  ledger->GetPage(
      convert::ToArray(id),
      [this](Status, mojo::InterfaceHandle<Page>) { message_loop_.QuitNow(); });
  message_loop_.Run();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(id, storage_ptr->get_page_calls[0]);
  EXPECT_EQ(0u, storage_ptr->delete_page_calls.size());
  storage_ptr->ClearCalls();

  ledger->DeletePage(convert::ToArray(id),
                     [this](Status) { message_loop_.QuitNow(); });
  message_loop_.Run();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->delete_page_calls.size());
  EXPECT_EQ(id, storage_ptr->delete_page_calls[0]);
  storage_ptr->ClearCalls();
}

// Verifies that deleting the LedgerManager closes the message pipes connected
// to LedgerImpl.
TEST_F(LedgerManagerTest, DeletingLedgerManagerClosesConnections) {
  std::unique_ptr<LedgerManager> ledger_manager =
      std::make_unique<LedgerManager>(std::make_unique<FakeLedgerStorage>());

  LedgerPtr ledger = ledger_manager->GetLedgerPtr();
  bool ledger_closed = false;
  ledger.set_connection_error_handler([this, &ledger_closed] {
    ledger_closed = true;
    message_loop_.QuitNow();
  });

  ledger_manager.reset();
  message_loop_.Run();
  EXPECT_TRUE(ledger_closed);
}

}  // namespace
}  // namespace ledger
