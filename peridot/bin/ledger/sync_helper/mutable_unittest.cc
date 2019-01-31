// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/sync_helper/mutable.h"

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(MutableTest, ConstIsMutable) {
  const auto foo = Mutable(false);
  EXPECT_FALSE(*foo);
  *foo = true;
  EXPECT_TRUE(*foo);
}

}  // namespace
}  // namespace ledger
