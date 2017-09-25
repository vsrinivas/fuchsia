// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/bin/ledger/test/integration/integration_test.h"
#include "peridot/bin/ledger/test/integration/test_utils.h"

namespace test {
namespace integration {
namespace {

class PageIntegrationTest : public IntegrationTest {
 public:
  PageIntegrationTest() {}
  ~PageIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageIntegrationTest);
};

TEST_F(PageIntegrationTest, LedgerRepositoryDuplicate) {
  auto instance = NewLedgerAppInstance();

  files::ScopedTempDir tmp_dir;
  ledger::LedgerRepositoryPtr repository = instance->GetTestLedgerRepository();

  ledger::Status status;
  ledger::LedgerRepositoryPtr duplicated_repository;
  repository->Duplicate(duplicated_repository.NewRequest(),
                        [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponse());
  EXPECT_EQ(ledger::Status::OK, status);
}

TEST_F(PageIntegrationTest, GetLedger) {
  auto instance = NewLedgerAppInstance();
  EXPECT_TRUE(instance->GetTestLedger());
}

TEST_F(PageIntegrationTest, GetRootPage) {
  auto instance = NewLedgerAppInstance();
  ledger::LedgerPtr ledger = instance->GetTestLedger();
  ledger::Status status;
  ledger::PagePtr page;
  ledger->GetRootPage(page.NewRequest(),
                      callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);
}

TEST_F(PageIntegrationTest, NewPage) {
  auto instance = NewLedgerAppInstance();
  // Get two pages and check that their ids are different.
  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> id1 = PageGetId(&page1);
  ledger::PagePtr page2 = instance->GetTestPage();
  fidl::Array<uint8_t> id2 = PageGetId(&page2);

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(PageIntegrationTest, GetPage) {
  auto instance = NewLedgerAppInstance();
  // Create a page and expect to find it by its id.
  ledger::PagePtr page = instance->GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);
  instance->GetPage(id, ledger::Status::OK);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Search with a random id and expect a PAGE_NOT_FOUND result.
  fidl::Array<uint8_t> test_id = RandomArray(16);
  instance->GetPage(test_id, ledger::Status::PAGE_NOT_FOUND);
#endif
}

// Verifies that a page can be connected to twice.
TEST_F(PageIntegrationTest, MultiplePageConnections) {
  auto instance = NewLedgerAppInstance();
  // Create a new page and find its id.
  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  ledger::PagePtr page2 = instance->GetPage(page_id_1, ledger::Status::OK);
  fidl::Array<uint8_t> page_id_2 = PageGetId(&page2);
  EXPECT_EQ(convert::ToString(page_id_1), convert::ToString(page_id_2));
}

TEST_F(PageIntegrationTest, DeletePage) {
  auto instance = NewLedgerAppInstance();
  // Create a new page and find its id.
  ledger::PagePtr page = instance->GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed] { page_closed = true; });
  instance->DeletePage(id, ledger::Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page_closed);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Verify that the deleted page cannot be retrieved.
  instance->GetPage(id, ledger::Status::PAGE_NOT_FOUND);
#endif

  // Delete the same page again and expect a PAGE_NOT_FOUND result.
  instance->DeletePage(id, ledger::Status::PAGE_NOT_FOUND);
}

TEST_F(PageIntegrationTest, MultipleLedgerConnections) {
  auto instance = NewLedgerAppInstance();
  // Connect to the same ledger instance twice.
  ledger::LedgerPtr ledger_connection_1 = instance->GetTestLedger();
  ledger::LedgerPtr ledger_connection_2 = instance->GetTestLedger();

  // Create a page on the first connection.
  ledger::PagePtr page;
  ledger::Status status;
  ledger_connection_1->GetPage(nullptr, page.NewRequest(),
                               [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_1.WaitForIncomingResponse());
  EXPECT_EQ(ledger::Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  fidl::Array<uint8_t> id = PageGetId(&page);
  ledger_connection_2->DeletePage(std::move(id),
                                  [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_2.WaitForIncomingResponse());
  EXPECT_EQ(ledger::Status::OK, status);
}

}  // namespace
}  // namespace integration
}  // namespace test
