// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define FIDL_ENABLE_LEGACY_WAIT_FOR_RESPONSE

#include <utility>
#include <vector>

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace {

class PageIntegrationTest : public IntegrationTest {
 public:
  PageIntegrationTest() {}
  ~PageIntegrationTest() override {}

  // Returns the id of the given page.
  ledger::PageId PageGetId(ledger::PagePtr* page) {
    ledger::PageId id;
    (*page)->GetId(callback::Capture([this] { message_loop_.QuitNow(); }, &id));
    message_loop_.Run();
    return id;
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageIntegrationTest);
};

TEST_P(PageIntegrationTest, LedgerRepositoryDuplicate) {
  auto instance = NewLedgerAppInstance();

  files::ScopedTempDir tmp_dir;
  ledger_internal::LedgerRepositoryPtr repository =
      instance->GetTestLedgerRepository();

  ledger::Status status;
  ledger_internal::LedgerRepositoryPtr duplicated_repository;
  repository->Duplicate(duplicated_repository.NewRequest(),
                        [&status](ledger::Status s) { status = s; });
  EXPECT_EQ(ZX_OK, repository.WaitForResponse());
  EXPECT_EQ(ledger::Status::OK, status);
}

TEST_P(PageIntegrationTest, GetLedger) {
  auto instance = NewLedgerAppInstance();
  EXPECT_TRUE(instance->GetTestLedger());
}

TEST_P(PageIntegrationTest, GetRootPage) {
  auto instance = NewLedgerAppInstance();
  ledger::LedgerPtr ledger = instance->GetTestLedger();
  ledger::Status status;
  ledger::PagePtr page;
  ledger->GetRootPage(page.NewRequest(),
                      callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  EXPECT_EQ(ledger::Status::OK, status);
}

TEST_P(PageIntegrationTest, NewPage) {
  auto instance = NewLedgerAppInstance();
  // Get two pages and check that their ids are different.
  ledger::PagePtr page1 = instance->GetTestPage();
  ledger::PageId id1 = PageGetId(&page1);
  ledger::PagePtr page2 = instance->GetTestPage();
  ledger::PageId id2 = PageGetId(&page2);

  EXPECT_TRUE(id1.id != id2.id);
}

TEST_P(PageIntegrationTest, GetPage) {
  auto instance = NewLedgerAppInstance();
  // Create a page and expect to find it by its id.
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageId id = PageGetId(&page);
  instance->GetPage(fidl::MakeOptional(id), ledger::Status::OK);
}

// Verifies that a page can be connected to twice.
TEST_P(PageIntegrationTest, MultiplePageConnections) {
  auto instance = NewLedgerAppInstance();
  // Create a new page and find its id.
  ledger::PagePtr page1 = instance->GetTestPage();
  ledger::PageId page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  ledger::PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(page_id_1), ledger::Status::OK);
  ledger::PageId page_id_2 = PageGetId(&page2);
  EXPECT_EQ(page_id_1.id, page_id_2.id);
}

TEST_P(PageIntegrationTest, DeletePage) {
  auto instance = NewLedgerAppInstance();
  // Create a new page and find its id.
  ledger::PagePtr page = instance->GetTestPage();
  ledger::PageId id = PageGetId(&page);

  // Delete the page.
  bool page_closed = false;
  page.set_error_handler([&page_closed] { page_closed = true; });
  instance->DeletePage(id, ledger::Status::OK);

  // Verify that deletion of the page closed the page connection.
  EXPECT_FALSE(page);
  EXPECT_TRUE(page_closed);

  // Delete the same page again and expect a PAGE_NOT_FOUND result.
  instance->DeletePage(id, ledger::Status::PAGE_NOT_FOUND);
}

TEST_P(PageIntegrationTest, MultipleLedgerConnections) {
  auto instance = NewLedgerAppInstance();
  // Connect to the same ledger instance twice.
  ledger::LedgerPtr ledger_connection_1 = instance->GetTestLedger();
  ledger::LedgerPtr ledger_connection_2 = instance->GetTestLedger();

  // Create a page on the first connection.
  ledger::PagePtr page;
  ledger::Status status;
  ledger_connection_1->GetPage(nullptr, page.NewRequest(),
                               [&status](ledger::Status s) { status = s; });
  EXPECT_EQ(ZX_OK, ledger_connection_1.WaitForResponse());
  EXPECT_EQ(ledger::Status::OK, status);

  // Delete this page on the second connection and verify that the operation
  // succeeds.
  ledger::PageId id = PageGetId(&page);
  ledger_connection_2->DeletePage(std::move(id),
                                  [&status](ledger::Status s) { status = s; });
  EXPECT_EQ(ZX_OK, ledger_connection_2.WaitForResponse());
  EXPECT_EQ(ledger::Status::OK, status);
}

INSTANTIATE_TEST_CASE_P(PageIntegrationTest, PageIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace integration
}  // namespace test
