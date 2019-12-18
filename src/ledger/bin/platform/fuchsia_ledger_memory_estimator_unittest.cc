// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/platform/ledger_memory_estimator.h"
#include "src/ledger/bin/platform/platform.h"

namespace ledger {
namespace {

using ::testing::Gt;

TEST(FuchsiaLedgerMemoryEstimator, GetCurrentProcessMemoryUsage) {
  std::unique_ptr<Platform> platform = MakePlatform();
  uint64_t memory;
  ASSERT_TRUE(platform->memory_estimator()->GetCurrentProcessMemoryUsage(&memory));
  EXPECT_THAT(memory, Gt(0u));
}

}  // namespace
}  // namespace ledger
