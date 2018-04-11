// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"

namespace ledger {
namespace {

TEST(Environment, InitializationOfAsync) {
  fsl::MessageLoop loop;
  Environment env(loop.async());

  EXPECT_EQ(loop.async(), env.async());
}

}  // namespace
}  // namespace ledger
