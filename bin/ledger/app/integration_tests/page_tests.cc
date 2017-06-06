// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/integration_tests/integration_test.h"
#include "apps/ledger/src/app/integration_tests/test_utils.h"
#include "apps/ledger/src/convert/convert.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace integration_tests {
namespace {

class PageIntegrationTest : public IntegrationTest {
 public:
  PageIntegrationTest() {}
  ~PageIntegrationTest() override {}

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageIntegrationTest);
};

TEST_F(PageIntegrationTest, LedgerRepositoryDuplicate) {
  files::ScopedTempDir tmp_dir;
  Status status;
  LedgerRepositoryPtr repository;
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), nullptr, nullptr, repository.NewRequest(),
      [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_repository_factory_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  LedgerRepositoryPtr duplicated_repository;
  repository->Duplicate(duplicated_repository.NewRequest(),
                        [&status](Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageIntegrationTest, GetLedger) {
  EXPECT_NE(nullptr, ledger_.get());
}

TEST_F(PageIntegrationTest, GetRootPage) {
  Status status;
  PagePtr page;
  ledger_->GetRootPage(page.NewRequest(), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageIntegrationTest, NewPage) {
  // Get two pages and check that their ids are different.
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> id1 = PageGetId(&page1);
  PagePtr page2 = GetTestPage();
  fidl::Array<uint8_t> id2 = PageGetId(&page2);

  EXPECT_TRUE(!id1.Equals(id2));
}

TEST_F(PageIntegrationTest, GetPage) {
  // Create a page and expect to find it by its id.
  PagePtr page = GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);
  GetPage(id, Status::OK);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Search with a random id and expect a PAGE_NOT_FOUND result.
  fidl::Array<uint8_t> test_id = RandomArray(16);
  GetPage(test_id, Status::PAGE_NOT_FOUND);
#endif
}

// Verifies that a page can be connected to twice.
TEST_F(PageIntegrationTest, MultiplePageConnections) {
  // Create a new page and find its id.
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  PagePtr page2 = GetPage(page_id_1, Status::OK);
  fidl::Array<uint8_t> page_id_2 = PageGetId(&page2);
  EXPECT_EQ(convert::ToString(page_id_1), convert::ToString(page_id_2));
}

TEST_F(PageIntegrationTest, DeletePage) {
  // Create a new page and find its id.
  PagePtr page = GetTestPage();
  fidl::Array<uint8_t> id = PageGetId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_connection_error_handler([&page_closed] { page_closed = true; });
  DeletePage(id, Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page.WaitForIncomingResponse());
  EXPECT_TRUE(page_closed);

// TODO(etiennej): Reactivate after LE-87 is fixed.
#if 0
  // Verify that the deleted page cannot be retrieved.
  GetPage(id, Status::PAGE_NOT_FOUND);
#endif

  // Delete the same page again and expect a PAGE_NOT_FOUND result.
  DeletePage(id, Status::PAGE_NOT_FOUND);
}

TEST_F(PageIntegrationTest, MultipleLedgerConnections) {
  // Connect to the same ledger instance twice.
  LedgerPtr ledger_connection_1 = GetTestLedger();
  LedgerPtr ledger_connection_2 = GetTestLedger();

  // Create a page on the first connection.
  PagePtr page;
  Status status;
  ledger_connection_1->GetPage(nullptr, page.NewRequest(),
                               [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_1.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  fidl::Array<uint8_t> id = PageGetId(&page);
  ledger_connection_2->DeletePage(std::move(id),
                                  [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_connection_2.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

}  // namespace
}  // namespace integration_tests
}  // namespace ledger
