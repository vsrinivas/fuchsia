// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/metrics.h"

#include <cctype>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics::feedback_data {
namespace {

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

struct ExpectedMetric {
  std::string key;
  cobalt::TimedOutData metric;
  std::string name;
};

class AttachmentMetricsTest : public UnitTestFixture,
                              public ::testing::WithParamInterface<ExpectedMetric> {
 public:
  AttachmentMetricsTest() : cobalt_(dispatcher(), services(), &clock_) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  }

  cobalt::Logger* Cobalt() { return &cobalt_; }

 private:
  timekeeper::TestClock clock_;
  cobalt::Logger cobalt_;
};

INSTANTIATE_TEST_SUITE_P(VariousKeys, AttachmentMetricsTest,
                         ::testing::ValuesIn(std::vector<ExpectedMetric>({
                             {kAttachmentLogKernel, cobalt::TimedOutData::kKernelLog, "KernelLog"},
                             {kAttachmentLogSystem, cobalt::TimedOutData::kSystemLog, "SystemLog"},
                             {kAttachmentInspect, cobalt::TimedOutData::kInspect, "Inspect"},
                         })),
                         [](const ::testing::TestParamInfo<ExpectedMetric>& info) {
                           return info.param.name;
                         });

TEST_P(AttachmentMetricsTest, IndividualKeysTimeout) {
  const auto param = GetParam();

  AttachmentMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {param.key, Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({cobalt::Event(param.metric)}));
}

TEST_P(AttachmentMetricsTest, IndividualKeysNonTimeout) {
  const auto param = GetParam();

  AttachmentMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {param.key, Error::kMissingValue},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AttachmentMetricsTest, UnknownKey) {
  AttachmentMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {"unknown", Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AttachmentMetricsTest, NonTimeout) {
  AttachmentMetrics metrics(Cobalt());
  metrics.LogMetrics({
      {"unknown", Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(AttachmentMetricsTest, AllAttachments) {
  AttachmentMetrics metrics(Cobalt());

  metrics.LogMetrics({
      {kAttachmentLogKernel, Error::kTimeout},
      {kAttachmentLogSystem, Error::kTimeout},
      {kAttachmentInspect, Error::kTimeout},
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kKernelLog),
                                          cobalt::Event(cobalt::TimedOutData::kSystemLog),
                                          cobalt::Event(cobalt::TimedOutData::kInspect),
                                      }));
}

}  // namespace
}  // namespace forensics::feedback_data
