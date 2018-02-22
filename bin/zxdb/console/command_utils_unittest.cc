// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include "garnet/bin/zxdb/client/err.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(CommandUtils, StringToUint64) {
  uint64_t result = 0;
  EXPECT_FALSE(StringToUint64("1234", &result).has_error());
  EXPECT_EQ(1234u, result);

  EXPECT_FALSE(StringToUint64("0x1234", &result).has_error());
  EXPECT_EQ(0x1234u, result);

  // Max value.
  EXPECT_FALSE(StringToUint64("0xffffffffffffffff", &result).has_error());
  EXPECT_EQ(0xffffffffffffffffu, result);

  EXPECT_TRUE(StringToUint64("asdf", &result).has_error());
}

}  // namespace zxdb
