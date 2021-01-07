// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/device/metrics_reporter.h"

#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace camera {
namespace {

using ::inspect::testing::BoolIs;
using ::inspect::testing::ChildrenMatch;
using ::inspect::testing::NameMatches;
using ::inspect::testing::NodeMatches;
using ::inspect::testing::PropertyList;
using ::inspect::testing::StringIs;
using ::inspect::testing::UintIs;
using ::testing::AllOf;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;

class MetricsReporterTest : public ::gtest::TestLoopFixture {
 public:
  inspect::Hierarchy GetHierarchy() {
    zx::vmo duplicate = metrics_.inspector().DuplicateVmo();
    if (!duplicate) {
      return inspect::Hierarchy();
    }

    auto ret = inspect::ReadFromVmo(std::move(duplicate));
    EXPECT_TRUE(ret.is_ok());
    if (ret.is_ok()) {
      return ret.take_value();
    }

    return inspect::Hierarchy();
  }

 protected:
  sys::testing::ComponentContextProvider component_context_provider_;
  MetricsReporter metrics_{*component_context_provider_.context()};
};

TEST_F(MetricsReporterTest, InitialState) {
  auto hierarchy = GetHierarchy();

  // Expect metrics with default values in the root node.
  EXPECT_THAT(hierarchy, NodeMatches(NameMatches("root")));

  // Expect empty child nodes for streams
  EXPECT_THAT(
      hierarchy,
      ChildrenMatch(UnorderedElementsAre(AllOf(NodeMatches(AllOf(
          NameMatches("configurations"), PropertyList(IsEmpty()), PropertyList(IsEmpty())))))));
}

TEST_F(MetricsReporterTest, StreamFrameMetrics) {
  auto config = metrics_.CreateConfiguration(0, 3);

  // Expect nodes for each stream.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(AllOf(
          NodeMatches(NameMatches("configurations")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              NodeMatches(NameMatches("0")),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  NodeMatches(NameMatches("streams")),
                  ChildrenMatch(UnorderedElementsAre(
                      NodeMatches(AllOf(NameMatches("0"),
                                        PropertyList(IsSupersetOf({UintIs("frames received", 0),
                                                                   UintIs("frames dropped", 0)})))),
                      NodeMatches(AllOf(NameMatches("1"),
                                        PropertyList(IsSupersetOf({UintIs("frames received", 0),
                                                                   UintIs("frames dropped", 0)})))),
                      NodeMatches(AllOf(
                          NameMatches("2"),
                          PropertyList(IsSupersetOf({UintIs("frames received", 0),
                                                     UintIs("frames dropped", 0)}))))))))))))))));

  // Recevie 4 frames and drop 1 on stream 1
  config->stream(1).FrameReceived();
  config->stream(1).FrameReceived();
  config->stream(1).FrameReceived();
  config->stream(1).FrameReceived();
  config->stream(1).FrameDropped();

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(AllOf(
          NodeMatches(NameMatches("configurations")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              NodeMatches(NameMatches("0")),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  NodeMatches(NameMatches("streams")),
                  ChildrenMatch(UnorderedElementsAre(
                      NodeMatches(AllOf(NameMatches("0"),
                                        PropertyList(IsSupersetOf({UintIs("frames received", 0),
                                                                   UintIs("frames dropped", 0)})))),
                      NodeMatches(AllOf(NameMatches("1"),
                                        PropertyList(IsSupersetOf({UintIs("frames received", 4),
                                                                   UintIs("frames dropped", 1)})))),
                      NodeMatches(AllOf(
                          NameMatches("2"),
                          PropertyList(IsSupersetOf({UintIs("frames received", 0),
                                                     UintIs("frames dropped", 0)}))))))))))))))));
}

TEST_F(MetricsReporterTest, StreamProperties) {
  auto config = metrics_.CreateConfiguration(0, 1);
  fuchsia::camera3::StreamProperties2 properties;
  properties.set_frame_rate({30, 10});
  properties.set_supports_crop_region(true);
  properties.mutable_supported_resolutions()->push_back({1024, 768});
  properties.mutable_supported_resolutions()->push_back({1920, 1080});
  config->stream(0).SetProperties(properties);

  // Expect nodes for each stream.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(AllOf(
          NodeMatches(NameMatches("configurations")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              NodeMatches(NameMatches("0")),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  NodeMatches(NameMatches("streams")),
                  ChildrenMatch(UnorderedElementsAre(AllOf(
                      NodeMatches(AllOf(
                          NameMatches("0"),
                          PropertyList(IsSupersetOf({StringIs("frame rate", "30/10"),
                                                     BoolIs("supports crop region", true)})))),
                      ChildrenMatch(UnorderedElementsAre(
                          AllOf(NodeMatches(AllOf(NameMatches("supported resolutions"),
                                                  PropertyList(UnorderedElementsAre(
                                                      StringIs("1024x768", ""),
                                                      StringIs("1920x1080", "")))))))))))))))))))));
}

}  // namespace
}  // namespace camera
