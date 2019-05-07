// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/run_test_component.h"

#include <lib/async-loop/cpp/loop.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"

namespace run {
namespace {

TEST(RunTest, ParseArgs) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto env_services = sys::ServiceDirectory::CreateFromNamespace();
  constexpr char kBinName[] = "bin_name";

  constexpr char component_url[] =
      "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx";
  {
    const char* argv[] = {kBinName, component_url};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(0u, result.launch_info.arguments->size());
    EXPECT_EQ(0u, result.matching_urls.size());
  }

  {
    const char* argv[] = {kBinName, component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 4, argv);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[2], result.launch_info.arguments.get()[0]);
    EXPECT_EQ(argv[3], result.launch_info.arguments.get()[1]);
  }

  {
    const char* argv[] = {kBinName, "run_test_component_test_invalid_matcher"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_TRUE(result.error);
  }

  {
    std::string expected_urls[] = {
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/"
        "run_test_component_test.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx"};
    const char* argv[] = {kBinName, "run_test_component"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(2u, result.matching_urls.size());
    EXPECT_THAT(result.matching_urls,
                ::testing::UnorderedElementsAreArray(expected_urls));
  }

  {
    auto expected_url =
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx";

    const char* argv[] = {kBinName, "run_test_component_unittests"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error);
    ASSERT_EQ(1u, result.matching_urls.size());
    EXPECT_EQ(result.matching_urls[0], expected_url);
    EXPECT_EQ(expected_url, result.launch_info.url);
  }
}

}  // namespace
}  // namespace run
