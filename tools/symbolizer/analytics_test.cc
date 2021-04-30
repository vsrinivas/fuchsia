// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/analytics.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace symbolizer {

namespace {

using ::testing::ContainerEq;

TEST(AnalyticsTest, StackTraceHitBuilder) {
  StackTraceHitBuilder builder;
  builder.TotalTimerStart();
  builder.DownloadTimerStart();
  builder.SetAtLeastOneInvalidInputBit(true);
  builder.SetNumberOfModules(2);
  builder.SetNumberOfModulesWithLocalSymbols(3);
  builder.SetNumberOfModulesWithCachedSymbols(4);
  builder.SetNumberOfModulesWithDownloadedSymbols(5);
  builder.SetNumberOfModulesWithDownloadingFailure(6);
  builder.SetNumberOfFrames(7);
  builder.SetNumberOfFramesSymbolized(8);
  builder.SetNumberOfFramesInvalid(9);
  builder.SetRemoteSymbolLookupEnabledBit(false);
  builder.DownloadTimerStop();
  builder.TotalTimerStop();

  auto parameters = builder.build().parameters();
  ASSERT_EQ(parameters["utt"], parameters["plt"]);
  ASSERT_GE(std::stoll(parameters["plt"]), std::stoll(parameters["pdt"]));
  ASSERT_GE(std::stoll(parameters["pdt"]), 0LL);
  parameters["utt"] = "100";
  parameters["plt"] = "100";
  parameters["pdt"] = "50";

  const std::map<std::string, std::string> expected_result{
      {"t", "timing"}, {"utc", "symbolization"},
      {"utv", ""},     {"cm1", "1"},
      {"cm2", "2"},    {"cm3", "3"},
      {"cm4", "4"},    {"cm5", "5"},
      {"cm6", "6"},    {"cm7", "7"},
      {"cm8", "8"},    {"cm9", "9"},
      {"cm10", "0"},   {"utt", "100"},
      {"plt", "100"},  {"pdt", "50"}};
  EXPECT_THAT(parameters, ContainerEq(expected_result));
}

}  // namespace

}  // namespace symbolizer
