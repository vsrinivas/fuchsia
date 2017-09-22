// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/destruction_sentinel.h"

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(DestructionSentinel, CheckOnNormalOperation) {
  auto destruction_sentinel = std::make_unique<callback::DestructionSentinel>();
  EXPECT_FALSE(destruction_sentinel->DestructedWhile([] {}));
}

TEST(DestructionSentinel, CheckOnDestruction) {
  auto destruction_sentinel = std::make_unique<callback::DestructionSentinel>();
  EXPECT_TRUE(destruction_sentinel->DestructedWhile(
      [&destruction_sentinel] { destruction_sentinel.reset(); }));
}

}  // namespace
}  // namespace callback
