// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/test_with_ledger.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "peridot/lib/ledger_client/ledger_client.h"

namespace modular {
namespace testing {

TestWithLedger::TestWithLedger() = default;
TestWithLedger::~TestWithLedger() = default;

void TestWithLedger::SetUp() {
  RealLoopFixture::SetUp();

  ledger_app_ = std::make_unique<testing::LedgerRepositoryForTesting>();

  ledger_client_.reset(new LedgerClient(ledger_app_->ledger_repository(),
                                        __FILE__, [] { ASSERT_TRUE(false); }));
}

void TestWithLedger::TearDown() {
  ledger_client_.reset();

  bool terminated = false;
  ledger_app_->Terminate([&terminated] { terminated = true; });
  if (!terminated) {
    RunLoopWithTimeoutOrUntil([&terminated] { return terminated; });
  }

  ledger_app_.reset();

  RealLoopFixture::TearDown();
}

bool TestWithLedger::RunLoopWithTimeout(zx::duration timeout) {
  return RealLoopFixture::RunLoopWithTimeout(timeout);
}

bool TestWithLedger::RunLoopWithTimeoutOrUntil(fit::function<bool()> condition,
                                               zx::duration timeout) {
  return RealLoopFixture::RunLoopWithTimeoutOrUntil(std::move(condition),
                                                    timeout);
}

}  // namespace testing
}  // namespace modular
