// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>

#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace ledger {
namespace {

class IntegrationTestTests : public IntegrationTest {};

TEST_P(IntegrationTestTests, MultipleLedgerAppInstances) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  EXPECT_TRUE(instance1->GetTestLedger());
  EXPECT_TRUE(instance2->GetTestLedger());
}

INSTANTIATE_TEST_SUITE_P(IntegrationTestTests, IntegrationTestTests,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
