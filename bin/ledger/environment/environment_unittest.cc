// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(Environment, InitializationOfAsync) {
  async::Loop loop;
  Environment env(loop.async());

  EXPECT_EQ(loop.async(), env.async());
}

}  // namespace
}  // namespace ledger
