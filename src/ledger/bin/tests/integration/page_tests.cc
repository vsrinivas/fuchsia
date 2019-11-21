// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>

#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fxl/macros.h"

namespace ledger {
namespace {

class PageIntegrationTest : public IntegrationTest {
 public:
  PageIntegrationTest() = default;
  ~PageIntegrationTest() override = default;

  // Returns the id of the given page.
  PageId PageGetId(PagePtr* page) {
    PageId id;
    auto loop_waiter = NewWaiter();
    (*page)->GetId(callback::Capture(loop_waiter->GetCallback(), &id));
    EXPECT_TRUE(loop_waiter->RunUntilCalled());
    return id;
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageIntegrationTest);
};

TEST_P(PageIntegrationTest, LedgerRepositoryDuplicate) {
  auto instance = NewLedgerAppInstance();

  ledger_internal::LedgerRepositoryPtr repository = instance->GetTestLedgerRepository();

  ledger_internal::LedgerRepositoryPtr duplicated_repository;
  repository->Duplicate(duplicated_repository.NewRequest());
  auto loop_waiter = NewWaiter();
  duplicated_repository->Sync(loop_waiter->GetCallback());
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
}

TEST_P(PageIntegrationTest, GetLedger) {
  auto instance = NewLedgerAppInstance();
  EXPECT_TRUE(instance->GetTestLedger());
}

TEST_P(PageIntegrationTest, GetRootPage) {
  auto instance = NewLedgerAppInstance();
  LedgerPtr ledger = instance->GetTestLedger();
  PagePtr page;
  ledger->GetRootPage(page.NewRequest());
  auto loop_waiter = NewWaiter();
  ledger->Sync(loop_waiter->GetCallback());
  ASSERT_TRUE(loop_waiter->RunUntilCalled());
}

TEST_P(PageIntegrationTest, NewPage) {
  auto instance = NewLedgerAppInstance();
  // Get two pages and check that their ids are different.
  PagePtr page1 = instance->GetTestPage();
  PageId id1 = PageGetId(&page1);
  PagePtr page2 = instance->GetTestPage();
  PageId id2 = PageGetId(&page2);

  EXPECT_TRUE(id1.id != id2.id);
}

TEST_P(PageIntegrationTest, GetPage) {
  auto instance = NewLedgerAppInstance();
  // Create a page and expect to find it by its id.
  PagePtr page = instance->GetTestPage();
  PageId id = PageGetId(&page);
  instance->GetPage(fidl::MakeOptional(id));
}

// Verifies that a page can be connected to twice.
TEST_P(PageIntegrationTest, MultiplePageConnections) {
  auto instance = NewLedgerAppInstance();
  // Create a new page and find its id.
  PagePtr page1 = instance->GetTestPage();
  PageId page_id_1 = PageGetId(&page1);

  // Connect to the same page again.
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(page_id_1));
  PageId page_id_2 = PageGetId(&page2);
  EXPECT_EQ(page_id_2.id, page_id_1.id);
}

INSTANTIATE_TEST_SUITE_P(PageIntegrationTest, PageIntegrationTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
