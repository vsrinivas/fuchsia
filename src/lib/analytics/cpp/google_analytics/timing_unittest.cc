// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/timing.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace analytics::google_analytics {

using ::testing::ContainerEq;

TEST(TimingTest, All) {
  Timing timing1("category1", "variable1", 10);
  const std::map<std::string, std::string> expected_result1{
      {"t", "timing"}, {"utc", "category1"}, {"utv", "variable1"}, {"utt", "10"}};
  EXPECT_THAT(timing1.parameters(), ContainerEq(expected_result1));

  Timing timing2("category2", "variable2", 20, "label2");
  const std::map<std::string, std::string> expected_result2{{"t", "timing"},
                                                            {"utc", "category2"},
                                                            {"utv", "variable2"},
                                                            {"utt", "20"},
                                                            {"utl", "label2"}};
  EXPECT_THAT(timing2.parameters(), ContainerEq(expected_result2));

  Timing timing3("category3", "variable3", 30, "label3");
  timing3.SetPageLoadTime(1);
  timing3.SetDnsTime(2);
  timing3.SetPageDownloadTime(3);
  timing3.SetRedirectResponseTime(4);
  timing3.SetTcpConnectTime(5);
  timing3.SetServerResponseTime(6);
  timing3.SetDomInteractiveTime(7);
  timing3.SetContentLoadTime(8);
  const std::map<std::string, std::string> expected_result3{
      {"t", "timing"}, {"utc", "category3"}, {"utv", "variable3"}, {"utt", "30"}, {"utl", "label3"},
      {"plt", "1"},    {"dns", "2"},         {"pdt", "3"},         {"rrt", "4"},  {"tcp", "5"},
      {"srt", "6"},    {"dit", "7"},         {"clt", "8"}};
  EXPECT_THAT(timing3.parameters(), ContainerEq(expected_result3));
}

}  // namespace analytics::google_analytics
