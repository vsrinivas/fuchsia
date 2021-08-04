// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/diagnostics_impl.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdio>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace cobalt {

using inspect::testing::ChildrenMatch;
using inspect::testing::IntIs;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using ::testing::IsSupersetOf;
using ::testing::UnorderedElementsAre;

class DiagnosticsTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    listener_ = std::make_unique<DiagnosticsImpl>(inspector_.GetRoot().CreateChild("core"));
  }

  inspect::Hierarchy InspectHierarchy() {
    fpromise::result<inspect::Hierarchy> result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
    return result.take_value();
  }

 protected:
  std::unique_ptr<DiagnosticsImpl> listener_;
  inspect::Inspector inspector_;
};

TEST_F(DiagnosticsTest, SentObservationResultSuccess) {
  listener_->SentObservationResult(util::Status::OK);
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(AllOf(
                        NodeMatches(NameMatches("core")),
                        ChildrenMatch(Contains(AllOf(NodeMatches(AllOf(
                            NameMatches("sending_observations"),
                            PropertyList(IsSupersetOf({IntIs("successes", 1), IntIs("errors", 0),
                                                       IntIs("last_success_time", testing::Gt(0)),
                                                       IntIs("last_error_time", 0)}))))))))))));
}

TEST_F(DiagnosticsTest, SentObservationResultError) {
  listener_->SentObservationResult(
      util::Status(util::StatusCode::DEADLINE_EXCEEDED, "error_message", "error_details"));
  EXPECT_THAT(
      InspectHierarchy(),
      AllOf(NodeMatches(NameMatches("root")),
            ChildrenMatch(UnorderedElementsAre(AllOf(
                NodeMatches(NameMatches("core")),
                ChildrenMatch(Contains(AllOf(NodeMatches(AllOf(
                    NameMatches("sending_observations"),
                    PropertyList(UnorderedElementsAre(
                        IntIs("successes", 0), IntIs("errors", 1), IntIs("last_success_time", 0),
                        IntIs("last_error_time", testing::Gt(0)),
                        IntIs("last_error_code", util::StatusCode::DEADLINE_EXCEEDED),
                        StringIs("last_error_message", "error_message"),
                        StringIs("last_error_details", "error_details")))))))))))));
}

TEST_F(DiagnosticsTest, ObservationStoreUpdatedOnce) {
  std::map<ReportSpec, uint64_t> num_obs_per_report;
  num_obs_per_report[{.customer_id = 1, .project_id = 2, .metric_id = 3, .report_id = 4}] = 5;
  num_obs_per_report[{.customer_id = 6, .project_id = 7, .metric_id = 8, .report_id = 9}] = 10;
  listener_->ObservationStoreUpdated(num_obs_per_report, 1024, 2048);
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(
                        AllOf(NodeMatches(NameMatches("core")),
                              ChildrenMatch(Contains(AllOf(NodeMatches(AllOf(
                                  NameMatches("observations_stored"),
                                  PropertyList(UnorderedElementsAre(
                                      IntIs("byte_count", 1024), IntIs("byte_count_limit", 2048),
                                      IntIs("report_1-2-3-4", 5), IntIs("report_6-7-8-9", 10),
                                      IntIs("total", 15)))))))))))));
}

TEST_F(DiagnosticsTest, ObservationStoreUpdatedMultipleTimes) {
  std::map<ReportSpec, uint64_t> num_obs_per_report;
  num_obs_per_report[{.customer_id = 1, .project_id = 2, .metric_id = 3, .report_id = 4}] = 5;
  listener_->ObservationStoreUpdated(num_obs_per_report, 1024, 2048);
  num_obs_per_report[{.customer_id = 1, .project_id = 2, .metric_id = 3, .report_id = 4}] = 12;
  listener_->ObservationStoreUpdated(num_obs_per_report, 4096, 8192);
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(
                        AllOf(NodeMatches(NameMatches("core")),
                              ChildrenMatch(Contains(AllOf(NodeMatches(AllOf(
                                  NameMatches("observations_stored"),
                                  PropertyList(UnorderedElementsAre(
                                      IntIs("byte_count", 4096), IntIs("byte_count_limit", 8192),
                                      IntIs("report_1-2-3-4", 12), IntIs("total", 12)))))))))))));
}

}  // namespace cobalt
