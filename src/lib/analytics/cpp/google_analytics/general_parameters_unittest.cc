// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/general_parameters.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace analytics::google_analytics {

class GeneralParametersTester : public GeneralParameters {
 public:
  using GeneralParameters::SetApplicationName;
  using GeneralParameters::SetApplicationVersion;
  using GeneralParameters::SetCustomDimension;
  using GeneralParameters::SetCustomMetric;
};

using ::testing::ContainerEq;

TEST(GeneralParametersTest, SetCustomDimension) {
  GeneralParametersTester parameters;
  parameters.SetCustomDimension(1, "value1");
  parameters.SetCustomDimension(4, "value4");

  const std::map<std::string, std::string> expected_result{{"cd1", "value1"}, {"cd4", "value4"}};
  EXPECT_THAT(parameters.parameters(), ContainerEq(expected_result));
}

TEST(GeneralParametersTest, SetCustomMetric) {
  GeneralParametersTester parameters;
  parameters.SetCustomMetric(1, 1);
  parameters.SetCustomMetric(4, 4);

  const std::map<std::string, std::string> expected_result{{"cm1", "1"}, {"cm4", "4"}};
  EXPECT_THAT(parameters.parameters(), ContainerEq(expected_result));
}

// SetApplicationName() and SetApplicationVersion() are expected to be used together
TEST(GeneralParametersTest, SetApplicationNameVersion) {
  GeneralParametersTester parameters;
  parameters.SetApplicationName("fuchsia");
  parameters.SetApplicationVersion("1.0");

  const std::map<std::string, std::string> expected_result{{"an", "fuchsia"}, {"av", "1.0"}};
  EXPECT_THAT(parameters.parameters(), ContainerEq(expected_result));
}

}  // namespace analytics::google_analytics
