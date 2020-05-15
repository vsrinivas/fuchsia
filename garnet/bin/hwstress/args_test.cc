// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <string>

#include <fbl/span.h>
#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(Args, ParseHelp) {
  EXPECT_TRUE(ParseArgs({{"hwstress", "--help"}})->help);
  EXPECT_TRUE(ParseArgs({{"hwstress", "-h"}})->help);
}

TEST(Args, ParseDuration) {
  // Good duration specified.
  EXPECT_EQ(ParseArgs({{"hwstress", "-d", "5"}})->test_duration_seconds, 5.0);
  EXPECT_EQ(ParseArgs({{"hwstress", "-d", "0.1"}})->test_duration_seconds, 0.1);
  EXPECT_EQ(ParseArgs({{"hwstress", "--duration", "3"}})->test_duration_seconds, 3.0);

  // Bad durations.
  EXPECT_TRUE(ParseArgs({{"hwstress", "-d", "x"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "-d", "-3"}}).is_error());
}

}  // namespace
}  // namespace hwstress
