// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/builder.h"

namespace fidl {
namespace test {
namespace misc {
namespace {

TEST(Defaults, Various) {
  VariousDefaults value;
  EXPECT_EQ(value.int64_with_default, static_cast<int64_t>(5));
  EXPECT_STR_EQ(value.string_with_default.c_str(), "stuff");
  EXPECT_EQ(value.strict_enum_with_default, StrictEnum::MEMBER_B);
  EXPECT_EQ(value.flexible_enum_with_default, FlexibleEnum::MEMBER_B);
  EXPECT_TRUE(value.bool_with_default);
}

}  // namespace
}  // namespace misc
}  // namespace test
}  // namespace fidl
