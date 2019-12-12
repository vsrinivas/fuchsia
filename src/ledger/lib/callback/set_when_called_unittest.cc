// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "set_when_called.h"

#include <gtest/gtest.h>

namespace ledger {
namespace {

TEST(SetWhenCalled, SetsTheInitialValueToFalse) {
  bool called = true;
  auto callback = SetWhenCalled(&called);
  EXPECT_FALSE(called);
}

TEST(SetWhenCalled, SetsTheValueToTrueWhenCalled) {
  bool called = false;
  auto callback = SetWhenCalled(&called);
  EXPECT_FALSE(called);
  callback();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace ledger
