// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/devices/bin/driver_playground/src/playground_utils.h"

TEST(DriverPlaygroundTests, TestExtractArgv) {
  fidl::Arena arena;
  fidl::StringView arg1(arena, "first");
  fidl::StringView arg2(arena, "second");
  std::vector<fidl::StringView> args;
  args.emplace_back(arg1);
  args.emplace_back(arg2);
  fidl::VectorView<fidl::StringView> args_fidl(arena, args);
  std::vector<std::string> str_args = playground_utils::ExtractStringArgs("tool", args_fidl);
  std::vector<const char *> argv = playground_utils::ConvertToArgv(str_args);
  ASSERT_EQ(4ul, argv.size());
  ASSERT_STREQ("tool", argv[0]);
  ASSERT_STREQ("first", argv[1]);
  ASSERT_STREQ("second", argv[2]);
  ASSERT_EQ(nullptr, argv[3]);
}

TEST(DriverPlaygroundTests, TestGetNameForResolve) {
  const std::string kDefaultPackageUrl = "fuchsia-pkg://prefix/";
  const std::string kCustomPackageUrl = "fuchsia-pkg://mypackage/";
  const std::string kBootUrl = "fuchsia-boot:///";
  const std::string kToolName = "fuchsia-pkg-tool";

  std::string name_for_resolve = playground_utils::GetNameForResolve(kDefaultPackageUrl, kToolName);
  ASSERT_EQ(kDefaultPackageUrl + kToolName, name_for_resolve);

  name_for_resolve =
      playground_utils::GetNameForResolve(kDefaultPackageUrl, kCustomPackageUrl + kToolName);
  ASSERT_EQ(kCustomPackageUrl + kToolName, name_for_resolve);

  name_for_resolve = playground_utils::GetNameForResolve(kDefaultPackageUrl, kBootUrl + kToolName);
  ASSERT_EQ(kBootUrl + kToolName, name_for_resolve);
}
