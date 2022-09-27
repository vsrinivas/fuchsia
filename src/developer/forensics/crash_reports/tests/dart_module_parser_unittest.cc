// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/dart_module_parser.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::crash_reports {
namespace {

std::string BuildStackTrace(std::optional<std::string> build_id,
                            std::optional<std::string> isolate_dso_base,
                            std::vector<std::string> stack_trace_addrs) {
  std::string stack_trace =
      R"(*** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***
pid: 3436, tid: 547903581360, name io.flutter.ui
os: fuchsia arch: x86 comp: no sim: no
)";

  if (build_id.has_value()) {
    stack_trace += fxl::StringPrintf("build_id: '%s'\n", build_id->c_str());
  }

  if (isolate_dso_base.has_value()) {
    stack_trace += fxl::StringPrintf("isolate_dso_base: %s, vm_dso_base: 7f91994000\n",
                                     isolate_dso_base->c_str());
  }

  for (size_t i = 0; i < stack_trace_addrs.size(); ++i) {
    stack_trace += fxl::StringPrintf(
        "#%02zu abs %s virt 00000000013f7467 _kDartIsolateSnapshotInstructions+0x25e9c7\n", i,
        stack_trace_addrs[i].c_str());
  }

  return stack_trace;
}

TEST(DartModulesParserTest, WellFormedStackTrace) {
  const auto [is_unsymbolicated, modules] = ParseDartModulesFromStackTrace(
      BuildStackTrace(/*build_id=*/"0a1b2c3d4e5f0f1e2d3c4b5af0e1d2c3",
                      /*isolate_dso_base=*/"7f91994000",
                      /*stack_trace_addrs=*/
                      {
                          "0000007f92d8b467",
                          "0000007f92d8b2cb",
                          "0000007f92d89eb3",
                          "0000007f92d89c9f",
                          "0000007f92d8910f",
                          "0000007f92d8904b",
                          "0000007f9412cd87",
                          "0000007f92d88ea7",
                          "0000007f9336e88f",
                          "0000007f9336e557",
                      }));

  EXPECT_TRUE(is_unsymbolicated);
  ASSERT_TRUE(modules.has_value());
  EXPECT_EQ(*modules, "7f91994000,2798d88,<_>,3D2C1B0A5F4E1E0F2D3C4B5AF0E1D2C30");
}

TEST(DartModulesParserTest, BadBuildId) {
  const auto [is_unsymbolicated, modules] =
      ParseDartModulesFromStackTrace(BuildStackTrace(/*build_id=*/"0",
                                                     /*isolate_dso_base=*/"7f91994000",
                                                     /*stack_trace_addrs=*/
                                                     {
                                                         "0000007f92d8b467",
                                                         "0000007f92d8b2cb",
                                                         "0000007f92d89eb3",
                                                         "0000007f92d89c9f",
                                                         "0000007f92d8910f",
                                                         "0000007f92d8904b",
                                                         "0000007f9412cd87",
                                                         "0000007f92d88ea7",
                                                         "0000007f9336e88f",
                                                         "0000007f9336e557",
                                                     }));

  EXPECT_TRUE(is_unsymbolicated);
  EXPECT_FALSE(modules.has_value());
}

TEST(DartModulesParserTest, MissingBuildId) {
  const auto [is_unsymbolicated, modules] =
      ParseDartModulesFromStackTrace(BuildStackTrace(/*build_id=*/std::nullopt,
                                                     /*isolate_dso_base=*/"7f91994000",
                                                     /*stack_trace_addrs=*/
                                                     {
                                                         "0000007f92d8b467",
                                                         "0000007f92d8b2cb",
                                                         "0000007f92d89eb3",
                                                         "0000007f92d89c9f",
                                                         "0000007f92d8910f",
                                                         "0000007f92d8904b",
                                                         "0000007f9412cd87",
                                                         "0000007f92d88ea7",
                                                         "0000007f9336e88f",
                                                         "0000007f9336e557",
                                                     }));

  EXPECT_TRUE(is_unsymbolicated);
  EXPECT_FALSE(modules.has_value());
}

TEST(DartModulesParserTest, MissingIsolateDsoBase) {
  const auto [is_unsymbolicated, modules] = ParseDartModulesFromStackTrace(
      BuildStackTrace(/*build_id=*/"0a1b2c3d4e5f0f1e2d3c4b5af0e1d2c3",
                      /*isolate_dso_base=*/std::nullopt,
                      /*stack_trace_addrs=*/
                      {
                          "0000007f92d8b467",
                          "0000007f92d8b2cb",
                          "0000007f92d89eb3",
                          "0000007f92d89c9f",
                          "0000007f92d8910f",
                          "0000007f92d8904b",
                          "0000007f9412cd87",
                          "0000007f92d88ea7",
                          "0000007f9336e88f",
                          "0000007f9336e557",
                      }));

  EXPECT_TRUE(is_unsymbolicated);
  EXPECT_FALSE(modules.has_value());
}

TEST(DartModulesParserTest, MissingStackTrace) {
  const auto [is_unsymbolicated, modules] = ParseDartModulesFromStackTrace(
      BuildStackTrace(/*build_id=*/"0a1b2c3d4e5f0f1e2d3c4b5af0e1d2c3",
                      /*isolate_dso_base=*/"7f91994000",
                      /*stack_trace_addrs=*/{}));

  EXPECT_TRUE(is_unsymbolicated);
  EXPECT_FALSE(modules.has_value());
}

TEST(DartModulesParserTest, DoesNotMatchUnsymbolicatedStackTrace) {
  const auto [is_unsymbolicated, modules] = ParseDartModulesFromStackTrace(R"(NOT UNSYMBOLICATED)");
  EXPECT_FALSE(is_unsymbolicated);
  EXPECT_FALSE(modules);
}

}  // namespace
}  // namespace forensics::crash_reports
