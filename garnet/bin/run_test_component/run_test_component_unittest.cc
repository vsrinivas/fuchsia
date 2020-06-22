// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/run_test_component.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/logger.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"

namespace run {
namespace {

TEST(RunTest, ParseArgs) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto env_services = sys::ServiceDirectory::CreateFromNamespace();
  constexpr char kBinName[] = "bin_name";

  constexpr char component_url[] = "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx";
  {
    const char* argv[] = {kBinName, component_url};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(0u, result.launch_info.arguments->size());
    EXPECT_EQ(0u, result.matching_urls.size());
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
  }

  {
    const char* argv[] = {kBinName, component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 4, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[3], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
  }

  {
    const char* argv[] = {kBinName, "--realm-label=kittens", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[3], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[4], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
  }

  {
    const char* argv[] = {
        kBinName, "--realm-label=kittens", "--timeout=30", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 6, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[4], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[5], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(30, result.timeout);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
    EXPECT_FALSE(result.restrict_logs);
  }

  {
    const char* argv[] = {kBinName, "--timeout=-1", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName, "--timeout=invalid", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName, "--timeout=100", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[3], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[4], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(100, result.timeout);
    EXPECT_FALSE(result.restrict_logs);
  }

  {
    auto flags = {"--restrict-logs", "--restrict-logs=true", "--restrict-logs=tRue",
                  "--restrict-logs=TRUE"};
    for (const auto& flag : flags) {
      const char* argv[] = {kBinName, flag, component_url, "myarg1", "myarg2"};
      auto result = ParseArgs(env_services, 5, argv);
      EXPECT_FALSE(result.error) << result.error_msg;
      EXPECT_EQ(component_url, result.launch_info.url);
      ASSERT_TRUE(result.launch_info.arguments.has_value());
      EXPECT_EQ(2u, result.launch_info.arguments->size());
      EXPECT_EQ(argv[3], result.launch_info.arguments->at(0));
      EXPECT_EQ(argv[4], result.launch_info.arguments->at(1));
      EXPECT_EQ("", result.realm_label);
      EXPECT_EQ(-1, result.timeout);
      EXPECT_TRUE(result.restrict_logs);
    }
  }
  {
    auto flags = {
        "--restrict-logs=false", "--restrict-logs=fAlse", "--restrict-logs=FALSE",
        "--restrict-logs=0",     "--restrict-logs=1",     "--restrict-logs=t",
        "--restrict-logs=f",
    };
    for (const auto& flag : flags) {
      const char* argv[] = {kBinName, flag, component_url, "myarg1", "myarg2"};
      auto result = ParseArgs(env_services, 5, argv);
      EXPECT_FALSE(result.error) << result.error_msg;
      EXPECT_EQ(component_url, result.launch_info.url);
      ASSERT_TRUE(result.launch_info.arguments.has_value());
      EXPECT_EQ(2u, result.launch_info.arguments->size());
      EXPECT_EQ(argv[3], result.launch_info.arguments->at(0));
      EXPECT_EQ(argv[4], result.launch_info.arguments->at(1));
      EXPECT_EQ("", result.realm_label);
      EXPECT_EQ(-1, result.timeout);
      EXPECT_FALSE(result.restrict_logs);
    }
  }

  {
    const char* argv[] = {kBinName, "--restrict-logsfalse", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }
  // timeout out of range
  {
    const char* argv[] = {kBinName, "--timeout=3000000000", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName, "--unknown-argument=gives_error", component_url, "myarg1",
                          "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName,          "--realm-label=kittens",
                          "--restrict-logs", "--min-severity-logs=WARN",
                          component_url,     "myarg1",
                          "myarg2"};
    auto result = ParseArgs(env_services, 7, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[5], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[6], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(FX_LOG_WARNING, result.min_log_severity);
    EXPECT_TRUE(result.restrict_logs);
  }

  {
    const char* argv[] = {kBinName,
                          "--min-severity-logs=WARN",
                          "--restrict-logs=true",
                          "--realm-label=kittens",
                          component_url,
                          "myarg1",
                          "myarg2"};
    auto result = ParseArgs(env_services, 7, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[5], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[6], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(FX_LOG_WARNING, result.min_log_severity);
    EXPECT_TRUE(result.restrict_logs);
  }

  {
    const char* argv[] = {kBinName, "--min-severity-logs=TRACE", component_url, "myarg1", "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[3], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[4], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
    EXPECT_FALSE(result.restrict_logs);
  }

  {
    const char* argv[] = {kBinName, "--min-severity-logs=invalid", component_url, "myarg1",
                          "myarg2"};
    auto result = ParseArgs(env_services, 5, argv);
    EXPECT_TRUE(result.error);
  }

  {
    const char* argv[] = {kBinName, "run_test_component_test_invalid_matcher"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<std::string> expected_urls = {
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/coverage_component.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/logging_component.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/component_to_test_standard_out.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/"
        "logging_component_max_severity_error.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/"
        "logging_component_max_severity_info.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/"
        "logging_component_max_severity_warn.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cmx"};
    const char* argv[] = {kBinName, "run_test_component"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(expected_urls.size(), result.matching_urls.size());
    EXPECT_THAT(result.matching_urls, ::testing::UnorderedElementsAreArray(expected_urls));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
    EXPECT_FALSE(result.restrict_logs);
  }

  {
    auto expected_url =
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx";

    const char* argv[] = {kBinName, "run_test_component_unittests"};
    auto result = ParseArgs(env_services, 2, argv);
    EXPECT_FALSE(result.error) << result.error_msg;
    ASSERT_EQ(1u, result.matching_urls.size());
    EXPECT_EQ(result.matching_urls[0], expected_url);
    EXPECT_EQ(expected_url, result.launch_info.url);
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
    EXPECT_FALSE(result.restrict_logs);
  }
}

TEST(UrlTest, SimplifiedUrl) {
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx",
            run::GetSimplifiedUrl(
                "fuchsia-pkg://fuchsia.com/"
                "my-pkg?hash=3204f2f24920e55bfbcb9c3a058ec2869f229b18d00ef1049ec3f47e5b7e4351#"
                "meta/my-component.cmx"));

  EXPECT_EQ("fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx",
            run::GetSimplifiedUrl("fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx"));
}

}  // namespace
}  // namespace run
