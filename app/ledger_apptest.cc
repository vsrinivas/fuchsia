// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
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

mojo::Array<uint8_t> GetPageId(PagePtr page) {
  mojo::Array<uint8_t> pageId;
  page->GetId([&pageId](mojo::Array<uint8_t> id) { pageId = std::move(id); });
  EXPECT_TRUE(page.WaitForIncomingResponse());
  return pageId;
}

class LedgerApplicationTest : public mojo::test::ApplicationTestBase {
 public:
  LedgerApplicationTest() : ApplicationTestBase() {}
  ~LedgerApplicationTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ApplicationTestBase::SetUp();

    ledger_ = GetTestLedger();
    std::srand(0);
  }

  void TearDown() override {
    // Delete all pages used in the test.
    for (int i = page_ids_.size() - 1; i >= 0; --i) {
      DeletePage(page_ids_[i], Status::OK);
    }

    ApplicationTestBase::TearDown();
  }

  PagePtr GetTestPage();
  PagePtr GetPage(const mojo::Array<uint8_t>& pageId, Status expected_status);
  void DeletePage(const mojo::Array<uint8_t>& pageId, Status expected_status);

  LedgerPtr ledger_;
  std::vector<mojo::Array<uint8_t>> page_ids_;

 private:
  LedgerPtr GetTestLedger();

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApplicationTest);
};

LedgerPtr LedgerApplicationTest::GetTestLedger() {
  LedgerFactoryPtr ledgerFactory;
  ConnectToService(shell(), "mojo:ledger_codex", GetProxy(&ledgerFactory));

  Status status;
  mojo::InterfaceHandle<Ledger> ledger;
  IdentityPtr identity = Identity::New();
  identity->user_id = RandomArray(1);
  ledgerFactory->GetLedger(
      std::move(identity),
      [&status, &ledger](Status s, mojo::InterfaceHandle<Ledger> l) {
        status = s;
        ledger = std::move(l);
      });
  EXPECT_TRUE(ledgerFactory.WaitForIncomingResponse());

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

  PagePtr pagePtr = mojo::InterfacePtr<Page>::Create(std::move(page));

  mojo::Array<uint8_t> pageId;
  pagePtr->GetId(
      [&pageId](mojo::Array<uint8_t> id) { pageId = std::move(id); });
  EXPECT_TRUE(pagePtr.WaitForIncomingResponse());
  page_ids_.push_back(std::move(pageId));

  return pagePtr;
}

PagePtr LedgerApplicationTest::GetPage(const mojo::Array<uint8_t>& pageId,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(pageId.Clone(),
                   [&status, &page](Status s, mojo::InterfaceHandle<Page> p) {
                     status = s;
                     page = std::move(p);
                   });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  PagePtr pagePtr = mojo::InterfacePtr<Page>::Create(std::move(page));
  EXPECT_EQ(expected_status == Status::OK, pagePtr.get() != nullptr);

  return pagePtr;
}

void LedgerApplicationTest::DeletePage(const mojo::Array<uint8_t>& pageId,
                                       Status expected_status) {
  mojo::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(pageId.Clone(),
                      [&status, &page](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(expected_status, status);

  page_ids_.erase(std::remove_if(page_ids_.begin(), page_ids_.end(),
                                 [&pageId](const mojo::Array<uint8_t>& id) {
                                   return id.Equals(pageId);
                                 }),
                  page_ids_.end());
}

TEST_F(LedgerApplicationTest, GetLedger) {
  EXPECT_NE(nullptr, ledger_.get());
}

TEST_F(LedgerApplicationTest, LedgerGetRootPage) {
  Status status;
  ledger_->GetRootPage(
      [&status](Status s, mojo::InterfaceHandle<Page> p) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(LedgerApplicationTest, LedgerNewPage) {
  // Get two pages and check that their ids are different.
  mojo::Array<uint8_t> id1 = GetPageId(GetTestPage());
  mojo::Array<uint8_t> id2 = GetPageId(GetTestPage());

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(LedgerApplicationTest, LedgerGetPage) {
  // Create a page and expect to find it by its id.
  mojo::Array<uint8_t> id = GetPageId(GetTestPage());
  GetPage(id, Status::OK);

  // Search with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> testId = RandomArray(16);
  GetPage(testId, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerApplicationTest, LedgerDeletePage) {
  // Create a page, remove it and expect it doesn't exist.
  mojo::Array<uint8_t> id = GetPageId(GetTestPage());
  PagePtr page = GetPage(id, Status::OK);
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed]() { page_closed = true; });
  DeletePage(id, Status::OK);
  EXPECT_FALSE(page.WaitForIncomingResponseWithTimeout(100000));
  // TODO(etiennej): page_closed should be true (connections should get closed).
  EXPECT_FALSE(page_closed);
  GetPage(id, Status::PAGE_NOT_FOUND);

  // Remove a page with a random id and expect a PAGE_NOT_FOUND result.
  mojo::Array<uint8_t> testId = RandomArray(16);
  DeletePage(testId, Status::PAGE_NOT_FOUND);
}

}  // namespace
}  // namespace ledger

MojoResult MojoMain(MojoHandle handle) {
  return mojo::test::RunAllTests(handle);
}
