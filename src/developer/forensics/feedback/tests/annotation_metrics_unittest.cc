// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cctype>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/metrics.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

struct ExpectedMetric {
  std::string key;
  cobalt::TimedOutData metric;
  std::string name;
};

class AnnotationMetricsTest : public UnitTestFixture,
                              public ::testing::WithParamInterface<ExpectedMetric> {
 public:
  AnnotationMetricsTest() : cobalt_(dispatcher(), services(), &clock_) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  }

  cobalt::Logger* Cobalt() { return &cobalt_; }

 private:
  timekeeper::TestClock clock_;
  cobalt::Logger cobalt_;
};

INSTANTIATE_TEST_SUITE_P(
    VariousKeys, AnnotationMetricsTest,
    ::testing::ValuesIn(std::vector<ExpectedMetric>({
        {kHardwareBoardNameKey, cobalt::TimedOutData::kBoardInfo, "BoardName"},
        {kHardwareBoardRevisionKey, cobalt::TimedOutData::kBoardInfo, "BoardRevision"},
        {kHardwareProductLanguageKey, cobalt::TimedOutData::kProductInfo, "ProductLanguage"},
        {kHardwareProductLocaleListKey, cobalt::TimedOutData::kProductInfo, "ProductLocalList"},
        {kHardwareProductManufacturerKey, cobalt::TimedOutData::kProductInfo,
         "ProductManufacturer"},
        {kHardwareProductModelKey, cobalt::TimedOutData::kProductInfo, "ProductModel"},
        {kHardwareProductNameKey, cobalt::TimedOutData::kProductInfo, "ProductName"},
        {kHardwareProductRegulatoryDomainKey, cobalt::TimedOutData::kProductInfo,
         "ProductRegulatoryDomain"},
        {kHardwareProductSKUKey, cobalt::TimedOutData::kProductInfo, "ProductSKU"},
        {kSystemUpdateChannelCurrentKey, cobalt::TimedOutData::kChannel, "CurrentChannel"},
        {kSystemUpdateChannelTargetKey, cobalt::TimedOutData::kChannel, "TargetChannel"},
    })),
    [](const ::testing::TestParamInfo<ExpectedMetric>& info) { return info.param.name; });

TEST_P(AnnotationMetricsTest, IndividualKeysTimeout) {
  const auto param = GetParam();

  AnnotationMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {param.key, Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({cobalt::Event(param.metric)}));
}

TEST_P(AnnotationMetricsTest, IndividualKeysNonTimeout) {
  const auto param = GetParam();

  AnnotationMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {param.key, Error::kMissingValue},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AnnotationMetricsTest, UnknownKey) {
  AnnotationMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {"unknown", Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AnnotationMetricsTest, NonTimeout) {
  AnnotationMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {"unknown", Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AnnotationMetricsTest, AllAnnotations) {
  AnnotationMetrics metrics(Cobalt());

  metrics.LogMetrics({
      {kHardwareBoardNameKey, Error::kTimeout},
      {kHardwareBoardRevisionKey, Error::kTimeout},
      {kHardwareProductLanguageKey, Error::kTimeout},
      {kHardwareProductLocaleListKey, Error::kTimeout},
      {kHardwareProductManufacturerKey, Error::kTimeout},
      {kHardwareProductModelKey, Error::kTimeout},
      {kHardwareProductNameKey, Error::kTimeout},
      {kHardwareProductRegulatoryDomainKey, Error::kTimeout},
      {kHardwareProductSKUKey, Error::kTimeout},
      {kSystemUpdateChannelCurrentKey, Error::kTimeout},
      {kSystemUpdateChannelTargetKey, Error::kTimeout},

  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kBoardInfo),
                                          cobalt::Event(cobalt::TimedOutData::kProductInfo),
                                          cobalt::Event(cobalt::TimedOutData::kChannel),
                                      }));
}

}  // namespace
}  // namespace forensics::feedback
