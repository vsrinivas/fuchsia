// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/device/metrics_reporter.h"

#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/camera/lib/cobalt_logger/metrics.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace camera {
namespace {

inline constexpr const char kRootInspectorNodeName[] = "root";

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
  MetricsReporterTest() {
    MetricsReporter::Initialize(*component_context_provider_.context(), false);
  }

  inspect::Hierarchy GetHierarchy() {
    zx::vmo duplicate = MetricsReporter::Get().inspector().DuplicateVmo();
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
};

TEST_F(MetricsReporterTest, InitialState) {
  auto hierarchy = GetHierarchy();

  // Expect metrics with default values in the root node.
  EXPECT_THAT(hierarchy, NodeMatches(NameMatches(kRootInspectorNodeName)));

  // Expect empty child nodes for streams
  EXPECT_THAT(hierarchy,
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  // configuration node
                  NodeMatches(AllOf(NameMatches(kConfigurationInspectorNodeName),
                                    PropertyList(IsEmpty()), PropertyList(IsEmpty())))))));
}

TEST_F(MetricsReporterTest, StreamFrameMetrics) {
  auto config = MetricsReporter::Get().CreateConfigurationRecord(0, 3);
  // Expect nodes for each stream.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(AllOf(
          // configuration node
          NodeMatches(NameMatches(kConfigurationInspectorNodeName)),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              // configuration 0
              NodeMatches(NameMatches("0")),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  // stream node
                  NodeMatches(NameMatches(kStreamInspectorNodeName)),
                  ChildrenMatch(UnorderedElementsAre(
                      // stream 0
                      NodeMatches(
                          AllOf(NameMatches("0"),
                                PropertyList(IsSupersetOf(
                                    {UintIs(kStreamInspectorFramesReceivedPropertyName, 0),
                                     UintIs(kStreamInspectorFramesDroppedPropertyName, 0)})))),
                      // stream 1
                      NodeMatches(
                          AllOf(NameMatches("1"),
                                PropertyList(IsSupersetOf(
                                    {UintIs(kStreamInspectorFramesReceivedPropertyName, 0),
                                     UintIs(kStreamInspectorFramesDroppedPropertyName, 0)})))),
                      // stream 2
                      NodeMatches(AllOf(NameMatches("2"),
                                        PropertyList(IsSupersetOf(
                                            {UintIs(kStreamInspectorFramesReceivedPropertyName, 0),
                                             UintIs(kStreamInspectorFramesDroppedPropertyName,
                                                    0)}))))))))))))))));

  // Receive 4 frames and drop 1 on stream 1
  config->GetStreamRecord(1).FrameReceived();
  config->GetStreamRecord(1).FrameReceived();
  config->GetStreamRecord(1).FrameReceived();
  config->GetStreamRecord(1).FrameReceived();
  config->GetStreamRecord(1).FrameDropped(cobalt::FrameDropReason::kGeneral);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(AllOf(
          // configuration node
          NodeMatches(NameMatches(kConfigurationInspectorNodeName)),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              // configuration 0
              NodeMatches(NameMatches("0")),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  // stream node
                  NodeMatches(NameMatches(kStreamInspectorNodeName)),
                  ChildrenMatch(UnorderedElementsAre(
                      // stream 0
                      NodeMatches(
                          AllOf(NameMatches("0"),
                                PropertyList(IsSupersetOf(
                                    {UintIs(kStreamInspectorFramesReceivedPropertyName, 0),
                                     UintIs(kStreamInspectorFramesDroppedPropertyName, 0)})))),
                      // stream 1
                      NodeMatches(
                          AllOf(NameMatches("1"),
                                PropertyList(IsSupersetOf(
                                    {UintIs(kStreamInspectorFramesReceivedPropertyName, 4),
                                     UintIs(kStreamInspectorFramesDroppedPropertyName, 1)})))),
                      // stream 2
                      NodeMatches(AllOf(NameMatches("2"),
                                        PropertyList(IsSupersetOf(
                                            {UintIs(kStreamInspectorFramesReceivedPropertyName, 0),
                                             UintIs(kStreamInspectorFramesDroppedPropertyName,
                                                    0)}))))))))))))))));
}

TEST_F(MetricsReporterTest, StreamProperties) {
  auto config = MetricsReporter::Get().CreateConfigurationRecord(0, 1);

  fuchsia::sysmem::ImageFormat_2 format = {
      .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::NV12,
                       .has_format_modifier = false},
      .coded_width = 1920,
      .coded_height = 1080,
      .bytes_per_row = 3840,
      .display_width = 1920,
      .display_height = 1080,
      .color_space = {.type = fuchsia::sysmem::ColorSpaceType::SRGB},
      .has_pixel_aspect_ratio = true,
      .pixel_aspect_ratio_width = 2,
      .pixel_aspect_ratio_height = 3};

  fuchsia::camera3::StreamProperties2 properties;
  properties.set_image_format(format);
  properties.set_frame_rate({30, 10});
  properties.set_supports_crop_region(true);
  properties.mutable_supported_resolutions()->push_back({1024, 768});
  properties.mutable_supported_resolutions()->push_back({1920, 1080});
  config->GetStreamRecord(0).SetProperties(properties);

  // Expect nodes for each stream.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(AllOf(
                  // configuration node
                  NodeMatches(NameMatches(kConfigurationInspectorNodeName)),
                  ChildrenMatch(UnorderedElementsAre(AllOf(
                      // configuration 0
                      NodeMatches(NameMatches("0")),
                      ChildrenMatch(UnorderedElementsAre(AllOf(
                          // stream node
                          NodeMatches(NameMatches(kStreamInspectorNodeName)),
                          ChildrenMatch(UnorderedElementsAre(AllOf(
                              // stream 0
                              NodeMatches(AllOf(
                                  NameMatches("0"),
                                  PropertyList(IsSupersetOf(
                                      {StringIs(kStreamInspectorFrameratePropertyName, "30/10"),
                                       BoolIs(kStreamInspectorCropPropertyName, true)})))),
                              ChildrenMatch(UnorderedElementsAre(
                                  AllOf(NodeMatches(AllOf(
                                      NameMatches(kStreamInspectorResolutionNodeName),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs("1024x768", ""), StringIs("1920x1080", "")))))),
                                  AllOf(NodeMatches(AllOf(
                                      NameMatches(kStreamInspectorImageFormatNodeName),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs(kFormatInspectorPixelformatPropertyName, "NV12"),
                                          StringIs(kFormatInspectorOutputResolutionPropertyName,
                                                   "1920x1080, stride = 3840"),
                                          StringIs(kFormatInspectorDisplayResolutionPropertyName,
                                                   "1920x1080, stride = 1920"),
                                          StringIs(kFormatInspectorColorSpacePropertyName, "SRGB"),
                                          StringIs(kFormatInspectorAspectRatioPropertyName,
                                                   "2x3"))))))))  // stream 0 ChildrenMatch
                              )))                                 // stream node ChildrenMatch
                          )))                                     // configuration 0 ChildrenMatch
                      )))  // configuration node ChildrenMatch
                  ))));
}

}  // namespace
}  // namespace camera
