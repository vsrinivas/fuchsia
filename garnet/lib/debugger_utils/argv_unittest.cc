// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/fxl/arraysize.h>

#include "garnet/lib/debugger_utils/argv.h"

namespace debugger_utils {
namespace {

TEST(Argv, BuildFromStringView) {
  const fxl::StringView no_args{""};
  const Argv expected_none{};
  EXPECT_EQ(BuildArgv(no_args), expected_none);

  const fxl::StringView one_arg{"--foo"};
  const Argv expected_one{"--foo"};
  EXPECT_EQ(BuildArgv(one_arg), expected_one);

  const fxl::StringView multiple_args{"a b c"};
  const Argv expected_multiple{"a", "b", "c"};
  EXPECT_EQ(BuildArgv(multiple_args), expected_multiple);
}

TEST(Argv, BuildFromCStrings) {
  const char* const no_args[] = {};
  const Argv expected_none{};
  EXPECT_EQ(BuildArgv(no_args, 0), expected_none);

  const char* const one_arg[] = {"--foo"};
  const Argv expected_one{"--foo"};
  EXPECT_EQ(BuildArgv(one_arg, arraysize(one_arg)), expected_one);

  const char* const multiple_args[] = {"a", "b", "c"};
  const Argv expected_multiple{"a", "b", "c"};
  EXPECT_EQ(BuildArgv(multiple_args, arraysize(multiple_args)),
            expected_multiple);
}

TEST(Argv, ArgvToString) {
  const std::string no_args{""};
  EXPECT_EQ(ArgvToString(BuildArgv(no_args)), no_args);

  const std::string one_arg{"--foo"};
  EXPECT_EQ(ArgvToString(BuildArgv(one_arg)), one_arg);

  const std::string multiple_args{"a b c"};
  EXPECT_EQ(ArgvToString(BuildArgv(multiple_args)), multiple_args);
}

}  // namespace
}  // namespace debugger_utils
