// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/exception.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace analytics::google_analytics {

using ::testing::ContainerEq;

TEST(ExceptionTest, All) {
  Exception exception1;
  const std::map<std::string, std::string> expected_result1{{"t", "exception"}};
  EXPECT_THAT(exception1.parameters(), ContainerEq(expected_result1));

  Exception exception2("description");
  const std::map<std::string, std::string> expected_result2{{"t", "exception"},
                                                            {"exd", "description"}};
  EXPECT_THAT(exception2.parameters(), ContainerEq(expected_result2));

  Exception exception3(std::nullopt, true);
  const std::map<std::string, std::string> expected_result3{{"t", "exception"}, {"exf", "1"}};
  EXPECT_THAT(exception3.parameters(), ContainerEq(expected_result3));

  Exception exception4("other", false);
  const std::map<std::string, std::string> expected_result4{
      {"t", "exception"}, {"exd", "other"}, {"exf", "0"}};
  EXPECT_THAT(exception4.parameters(), ContainerEq(expected_result4));
}

}  // namespace analytics::google_analytics
