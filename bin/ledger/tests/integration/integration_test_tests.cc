// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_printf.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {
namespace {

class IntegrationTestTests : public IntegrationTest {};

TEST_P(IntegrationTestTests, MultipleLedgerAppInstances) {
  auto instance1 = NewLedgerAppInstance();
  auto instance2 = NewLedgerAppInstance();

  EXPECT_TRUE(instance1->GetTestLedger());
  EXPECT_TRUE(instance2->GetTestLedger());
}

INSTANTIATE_TEST_CASE_P(IntegrationTestTests, IntegrationTestTests,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace integration
}  // namespace test
