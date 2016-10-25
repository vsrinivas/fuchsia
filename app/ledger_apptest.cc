// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/convert/convert.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/environment/logging.h"

namespace ledger {
namespace {

mojo::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array[i] = prefix[i];
  }
  for (size_t i = prefix.size(); i < size / 4; ++i) {
    int random = std::rand();
    for (size_t j = 0; j < 4 && 4 * i + j < size; ++j) {
      array[4 * i + j] = random & 0xFF;
      random = random >> 8;
    }
  }
  return array;
}

mojo::Array<uint8_t> RandomArray(int size) {
  return RandomArray(size, std::vector<uint8_t>());
}

mojo::Array<uint8_t> GetPageId(PagePtr* page) {
  mojo::Array<uint8_t> page_id;
  (*page)->GetId(
      [&page_id](mojo::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(page->WaitForIncomingResponse());
  return page_id;
}

class LedgerApplicationTest : public mojo::test::ApplicationTestBase {
 public:
  LedgerApplicationTest() {}
  ~LedgerApplicationTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ApplicationTestBase::SetUp();
    ConnectToService(shell(), "mojo:ledger_codex", GetProxy(&ledger_factory_));
    ledger_ = GetTestLedger();
    std::srand(0);
  }

  void TearDown() override {
    // Delete all pages used in the test.
    for (auto& page_id : page_ids_) {
      ledger_->DeletePage(std::move(page_id),
                          [](Status status) { EXPECT_EQ(Status::OK, status); });
      EXPECT_TRUE(ledger_.WaitForIncomingResponse());
    }

    ApplicationTestBase::TearDown();
  }

  LedgerPtr GetTestLedger();
  PagePtr GetTestPage();
  PagePtr GetPage(const mojo::Array<uint8_t>& page_id, Status expected_status);
  void DeletePage(const mojo::Array<uint8_t>& page_id, Status expected_status);

  LedgerFactoryPtr ledger_factory_;
  LedgerPtr ledger_;

 private:
  // Record ids of pages created for testing, so that we can delete them in
  // TearDown() in a somewhat desperate attempt to clean up the files created
  // for the test.
  // TODO(ppi): Configure ledger.mojo so that it knows to write to TempScopedDir
  // when run for testing and remove this accounting.
  std::vector<mojo::Array<uint8_t>> page_ids_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationTest);
};

LedgerPtr LedgerApplicationTest::GetTestLedger() {
  Status status;
  mojo::InterfaceHandle<Ledger> ledger;
  IdentityPtr identity = Identity::New();
  identity->user_id = RandomArray(1);
  identity->app_id = RandomArray(1);
  ledger_factory_->GetLedger(
      std::move(identity),
      [&status, &ledger](Status s, mojo::InterfaceHandle<Ledger> l) {
        status = s;
        ledger = std::move(l);
      });
  EXPECT_TRUE(ledger_factory_.WaitForIncomingResponse());

  EXPECT_EQ(Status::OK, status);
  return mojo::InterfacePtr<Ledger>::Create(std::move(ledger));
}

PagePtr LedgerApplicationTest::GetTestPage() {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->NewPage([&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
    status = s;
    page = std::move(p);
  });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  PagePtr page_ptr = mojo::InterfacePtr<Page>::Create(std::move(page));

  mojo::Array<uint8_t> page_id;
  page_ptr->GetId(
      [&page_id](mojo::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(page_ptr.WaitForIncomingResponse());
  page_ids_.push_back(std::move(page_id));

  return page_ptr;
}

PagePtr LedgerApplicationTest::GetPage(const mojo::Array<uint8_t>& page_id,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(page_id.Clone(),
                   [&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
                     status = s;
                     page = std::move(p);
                   });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  PagePtr page_ptr = mojo::InterfacePtr<Page>::Create(std::move(page));
  EXPECT_EQ(expected_status == Status::OK, page_ptr.get() != nullptr);

  return page_ptr;
}

void LedgerApplicationTest::DeletePage(const mojo::Array<uint8_t>& page_id,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(page_id.Clone(),
                      [&status, &page](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  page_ids_.erase(std::remove_if(page_ids_.begin(), page_ids_.end(),
                                 [&page_id](const mojo::Array<uint8_t>& id) {
                                   return id.Equals(page_id);
                                 }),
                  page_ids_.end());
}

TEST_F(LedgerApplicationTest, GetLedger) {
  EXPECT_NE(nullptr, ledger_.get());
}

TEST_F(LedgerApplicationTest, GetRootPage) {
  Status status;
  ledger_->GetRootPage(
      [&status](Status s, mojo::InterfaceHandle<Page> p) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(LedgerApplicationTest, NewPage) {
  // Get two pages and check that their ids are different.
  PagePtr page1 = GetTestPage();
  mojo::Array<uint8_t> id1 = GetPageId(&page1);
  PagePtr page2 = GetTestPage();
  mojo::Array<uint8_t> id2 = GetPageId(&page2);

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(LedgerApplicationTest, GetPage) {
  // Create a page and expect to find it by its id.
  PagePtr page = GetTestPage();
  mojo::Array<uint8_t> id = GetPageId(&page);
  GetPage(id, Status::OK);

  // Search with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> test_id = RandomArray(16);
  GetPage(test_id, Status::PAGE_NOT_FOUND);
}

// Verifies that a page can be connected to twice.
TEST_F(LedgerApplicationTest, MultiplePageConnections) {
  // Create a new page and find its id.
  PagePtr page1 = GetTestPage();
  mojo::Array<uint8_t> page_id_1 = GetPageId(&page1);

  // Connect to the same page again.
  PagePtr page2 = GetPage(page_id_1, Status::OK);
  mojo::Array<uint8_t> page_id_2 = GetPageId(&page2);
  EXPECT_EQ(convert::ToString(page_id_1), convert::ToString(page_id_2));
}

TEST_F(LedgerApplicationTest, DeletePage) {
  // Create a new page and find its id.
  PagePtr page = GetTestPage();
  mojo::Array<uint8_t> id = GetPageId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed] { page_closed = true; });
  DeletePage(id, Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page_closed);

  // Verify that the deleted page cannot be retrieved.
  GetPage(id, Status::PAGE_NOT_FOUND);

  // Delete the same page again and expect a PAGE_NOT_FOUND result.
  DeletePage(id, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerApplicationTest, MultipleLedgerConnections) {
  // Connect to the same ledger instance twice.
  LedgerPtr ledger_connection_1 = GetTestLedger();
  LedgerPtr ledger_connection_2 = GetTestLedger();

  // Create a page on the first connection.
  PagePtr page;
  Status status;
  ledger_connection_1->NewPage(
      [&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
        status = s;
        page = PagePtr::Create(std::move(p));
      });
  EXPECT_TRUE(ledger_connection_1.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  mojo::Array<uint8_t> id = GetPageId(&page);
  ledger_connection_2->DeletePage(std::move(id),
                                  [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_2.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

}  // namespace
}  // namespace ledger

MojoResult MojoMain(MojoHandle handle) {
  return mojo::test::RunAllTests(handle);
}
