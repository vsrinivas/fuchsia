// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/base/ownable.h"
#include "gtest/gtest.h"

namespace {
using namespace escher;

TEST(Ownable, FortyTwo) {
  Ownable ownable;
  EXPECT_EQ(42, ownable.FortyTwo());
  EXPECT_NE(666, ownable.FortyTwo());
}

}  // namespace
