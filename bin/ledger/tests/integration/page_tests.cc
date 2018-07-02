// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    (*page)->GetId(callback::Capture(QuitLoopClosure(), &id));
    RunLoop();
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

  ledger_internal::LedgerRepositoryPtr duplicated_repository;
  auto waiter = NewWaiter();
  ledger::Status status;
  repository->Duplicate(duplicated_repository.NewRequest(),
                        callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
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
                      callback::Capture(QuitLoopClosure(), &status));
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

INSTANTIATE_TEST_CASE_P(PageIntegrationTest, PageIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace integration
}  // namespace test
