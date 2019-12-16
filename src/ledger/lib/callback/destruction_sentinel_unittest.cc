// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/destruction_sentinel.h"

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(DestructionSentinel, CheckOnNormalOperation) {
  auto destruction_sentinel = std::make_unique<ledger::DestructionSentinel>();
  EXPECT_FALSE(destruction_sentinel->DestructedWhile([] {}));
}

TEST(DestructionSentinel, CheckOnDestruction) {
  auto destruction_sentinel = std::make_unique<ledger::DestructionSentinel>();
  EXPECT_TRUE(destruction_sentinel->DestructedWhile(
      [&destruction_sentinel] { destruction_sentinel.reset(); }));
}

TEST(DestructionSentinel, CheckReEntrancy) {
  auto destruction_sentinel = std::make_unique<ledger::DestructionSentinel>();
  EXPECT_FALSE(destruction_sentinel->DestructedWhile(
      [&destruction_sentinel] { EXPECT_FALSE(destruction_sentinel->DestructedWhile([] {})); }));

  EXPECT_TRUE(destruction_sentinel->DestructedWhile([&destruction_sentinel] {
    EXPECT_TRUE(destruction_sentinel->DestructedWhile(
        [&destruction_sentinel] { destruction_sentinel.reset(); }));
  }));

  destruction_sentinel = std::make_unique<ledger::DestructionSentinel>();
  EXPECT_TRUE(destruction_sentinel->DestructedWhile([&destruction_sentinel] {
    EXPECT_FALSE(destruction_sentinel->DestructedWhile([] {}));
    destruction_sentinel.reset();
  }));
}

}  // namespace
}  // namespace ledger
