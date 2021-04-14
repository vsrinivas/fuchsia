// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/event.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace analytics::google_analytics {

using ::testing::ContainerEq;

TEST(EventTest, All) {
  Event event1("category1", "action1");
  const std::map<std::string, std::string> expected_result1{
      {"t", "event"}, {"ec", "category1"}, {"ea", "action1"}};
  EXPECT_THAT(event1.parameters(), ContainerEq(expected_result1));

  Event event2("category2", "action2", "label2", 2);
  const std::map<std::string, std::string> expected_result2{
      {"t", "event"}, {"ec", "category2"}, {"ea", "action2"}, {"el", "label2"}, {"ev", "2"}};
  EXPECT_THAT(event2.parameters(), ContainerEq(expected_result2));
}

}  // namespace analytics::google_analytics
