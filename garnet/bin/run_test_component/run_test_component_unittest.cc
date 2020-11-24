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
    std::vector<const char*> argv = {kBinName, component_url};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    EXPECT_EQ(0u, result.launch_info.arguments->size());
    EXPECT_EQ(0u, result.matching_urls.size());
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(-1, result.wait_for_utc_timeout);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {kBinName, component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(-1, result.wait_for_utc_timeout);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--realm-label=kittens", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(-1, result.wait_for_utc_timeout);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {kBinName,       "--realm-label=kittens",
                                     "--timeout=30", "--wait-for-utc=10",
                                     component_url,  "--",
                                     "myarg1",       "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(30, result.timeout);
    EXPECT_EQ(10, result.wait_for_utc_timeout);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {kBinName, "--timeout=-1", component_url,
                                     "--",     "myarg1",       "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {kBinName, "--timeout=invalid", component_url, "--", "myarg1",
                                     "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--wait-for-utc-timeout=invalid", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {kBinName,      "--wait-for-utc-timeout=987654321987654321",
                                     component_url, "--",
                                     "myarg1",      "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {kBinName, "--timeout=100", component_url,
                                     "--",     "myarg1",        "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(100, result.timeout);
    EXPECT_EQ(-1, result.wait_for_utc_timeout);
  }

  // timeout out of range
  {
    std::vector<const char*> argv = {
        kBinName, "--timeout=3000000000", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--unknown-argument=gives_error", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {kBinName,
                                     "--realm-label=kittens",
                                     "--min-severity-logs=WARN",
                                     component_url,
                                     "--",
                                     "myarg1",
                                     "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(FX_LOG_WARNING, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {kBinName,
                                     "--min-severity-logs=INFO",
                                     "--realm-label=kittens",
                                     component_url,
                                     "--",
                                     "myarg1",
                                     "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("kittens", result.realm_label);
    EXPECT_EQ(FX_LOG_INFO, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--min-severity-logs=TRACE", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--min-severity-logs=invalid", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--max-log-severity=invalid", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<const char*> argv = {
        kBinName, "--max-log-severity=ERROR", component_url, "--", "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
    EXPECT_EQ(FX_LOG_ERROR, result.max_log_severity);
  }

  {
    std::vector<const char*> argv = {kBinName, "run_test_component_test_invalid_matcher"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_TRUE(result.error);
  }

  {
    std::vector<std::string> expected_urls = {
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/coverage_component.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/logging_component.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/component_to_test_standard_out.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/error_logging_component.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/logging_component_with_config.cmx",
        "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cmx"};
    std::vector<const char*> argv = {kBinName, "run_test_component"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(expected_urls.size(), result.matching_urls.size());
    EXPECT_THAT(result.matching_urls, ::testing::UnorderedElementsAreArray(expected_urls));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  {
    auto expected_url =
        "fuchsia-pkg://fuchsia.com/run_test_component_unittests#meta/"
        "run_test_component_unittests.cmx";

    std::vector<const char*> argv = {kBinName, "run_test_component_unittests"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    ASSERT_EQ(1u, result.matching_urls.size());
    EXPECT_EQ(result.matching_urls[0], expected_url);
    EXPECT_EQ(expected_url, result.launch_info.url);
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
  }

  // check that for time being we can pass tets args without using option delimiter(--).
  {
    std::vector<const char*> argv = {kBinName, component_url, "myarg1", "myarg2"};
    auto argc = argv.size();
    auto result = ParseArgs(env_services, argc, argv.data());
    EXPECT_FALSE(result.error) << result.error_msg;
    EXPECT_EQ(component_url, result.launch_info.url);
    ASSERT_TRUE(result.launch_info.arguments.has_value());
    EXPECT_EQ(2u, result.launch_info.arguments->size());
    EXPECT_EQ(argv[argc - 2], result.launch_info.arguments->at(0));
    EXPECT_EQ(argv[argc - 1], result.launch_info.arguments->at(1));
    EXPECT_EQ("", result.realm_label);
    EXPECT_EQ(-1, result.timeout);
    EXPECT_EQ(FX_LOG_TRACE, result.min_log_severity);
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
