// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/test_with_ledger.h"

#include <lib/fit/function.h>

#include <gtest/gtest.h>

namespace modular_testing {

TestWithLedger::TestWithLedger() {
  ledger_app_ = std::make_unique<modular_testing::LedgerRepositoryForTesting>();

  ledger_client_ = NewLedgerClient();
};

TestWithLedger::~TestWithLedger() {
  ledger_client_.reset();

  bool terminated = false;
  ledger_app_->Terminate([&terminated] { terminated = true; });
  if (!terminated) {
    RunLoopWithTimeoutOrUntil([&terminated] { return terminated; });
  }

  ledger_app_.reset();
};

std::unique_ptr<modular::LedgerClient> TestWithLedger::NewLedgerClient() {
  return std::make_unique<modular::LedgerClient>(
      ledger_app_->ledger_repository(), __FILE__,
      [](zx_status_t status) { ASSERT_TRUE(false) << "Status: " << status; });
}

bool TestWithLedger::RunLoopWithTimeout(zx::duration timeout) {
  return RealLoopFixture::RunLoopWithTimeout(timeout);
}

bool TestWithLedger::RunLoopWithTimeoutOrUntil(fit::function<bool()> condition,
                                               zx::duration timeout) {
  return RealLoopFixture::RunLoopWithTimeoutOrUntil(std::move(condition), timeout);
}

}  // namespace modular_testing
