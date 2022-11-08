// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/shared/reporter.h"

#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/shared/audio_admin.h"

namespace media::audio {
namespace {

using ::inspect::testing::BoolIs;
using ::inspect::testing::ChildrenMatch;
using ::inspect::testing::DoubleIs;
using ::inspect::testing::NameMatches;
using ::inspect::testing::NodeMatches;
using ::inspect::testing::PropertyList;
using ::inspect::testing::StringIs;
using ::inspect::testing::UintIs;
using ::testing::AllOf;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;

::testing::Matcher<const ::inspect::Hierarchy&> NodeAlive(const std::string& name) {
  return NodeMatches(
      AllOf(NameMatches(name), PropertyList(Contains(UintIs("time since death (ns)", 0)))));
}

::testing::Matcher<const ::inspect::Hierarchy&> NodeDead(const std::string& name) {
  return NodeMatches(
      AllOf(NameMatches(name), Not(PropertyList(Contains(UintIs("time since death (ns)", 0))))));
}

class ReporterTest : public gtest::TestLoopFixture {
 public:
  ReporterTest()
      : under_test_(*component_context_provider_.context(), dispatcher(), dispatcher(), false) {}

  inspect::Hierarchy GetHierarchy() {
    zx::vmo duplicate = under_test_.inspector().DuplicateVmo();
    if (duplicate.get() == ZX_HANDLE_INVALID) {
      return inspect::Hierarchy();
    }

    auto ret = inspect::ReadFromVmo(std::move(duplicate));
    EXPECT_TRUE(ret.is_ok());
    if (ret.is_ok()) {
      return ret.take_value();
    }

    return inspect::Hierarchy();
  }

  inspect::Hierarchy GetHierarchyLazyValues() {
    fpromise::result<inspect::Hierarchy> result;
    fpromise::single_threaded_executor exec;
    exec.schedule_task(
        inspect::ReadFromInspector(under_test_.inspector())
            .then([&](fpromise::result<inspect::Hierarchy>& res) { result = std::move(res); }));
    exec.run();
    EXPECT_TRUE(result.is_ok());
    return result.take_value();
  }

  sys::testing::ComponentContextProvider component_context_provider_;
  Reporter under_test_;
};

// Tests reporter initial state.
TEST_F(ReporterTest, InitialState) {
  auto hierarchy = GetHierarchy();

  // Expect metrics with default values in the root node.
  EXPECT_THAT(
      hierarchy,
      NodeMatches(AllOf(NameMatches("root"),
                        PropertyList(IsSupersetOf(
                            {UintIs("count of failures to open device", 0),
                             UintIs("count of failures to obtain device fdio service channel", 0),
                             UintIs("count of failures to obtain device stream channel", 0),
                             UintIs("count of failures to start a device", 0)})))));

  // Expect empty child nodes for devices and client ports.
  EXPECT_THAT(
      hierarchy,
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(AllOf(NameMatches("output devices"), PropertyList(IsEmpty()),
                                  PropertyList(IsEmpty()))),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("input devices"), PropertyList(IsEmpty()),
                                  PropertyList(IsEmpty()))),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("renderers"), PropertyList(IsEmpty()),
                                  PropertyList(IsEmpty()))),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("capturers"), PropertyList(IsEmpty()),
                                  PropertyList(IsEmpty()))),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(
                    AllOf(NameMatches("thermal state"),
                          PropertyList(UnorderedElementsAre(UintIs("num thermal states", 1))))),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(
                    AllOf(NameMatches("normal"),
                          Not(PropertyList(Contains(UintIs("total duration (ns)", 0))))))))),
          AllOf(
              NodeMatches(NameMatches("thermal state transitions")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(IsSupersetOf({BoolIs("active", true), StringIs("state", "normal")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0))))))))),
          AllOf(NodeMatches(AllOf(NameMatches("volume controls"), PropertyList(IsEmpty()),
                                  PropertyList(IsEmpty()))),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("active usage policies"),
                                  PropertyList(UnorderedElementsAre(
                                      DoubleIs("none gain db", 0.0), DoubleIs("duck gain db", 0.0),
                                      DoubleIs("mute gain db", 0.0))))),
                ChildrenMatch(Contains(NodeMatches(
                    AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", true)))))))))));
}

// Tests methods that update metrics in the root node.
TEST_F(ReporterTest, RootMetrics) {
  under_test_.FailedToOpenDevice("", false, 0);
  under_test_.FailedToObtainFdioServiceChannel("", false, 0);
  under_test_.FailedToObtainFdioServiceChannel("", false, 0);
  under_test_.FailedToObtainStreamChannel("", false, 0);
  under_test_.FailedToObtainStreamChannel("", false, 0);
  under_test_.FailedToObtainStreamChannel("", false, 0);
  under_test_.FailedToStartDevice("");
  under_test_.FailedToStartDevice("");
  under_test_.FailedToStartDevice("");
  under_test_.FailedToStartDevice("");

  EXPECT_THAT(
      GetHierarchy(),
      NodeMatches(AllOf(NameMatches("root"),
                        PropertyList(IsSupersetOf(
                            {UintIs("count of failures to open device", 1u),
                             UintIs("count of failures to obtain device fdio service channel", 2u),
                             UintIs("count of failures to obtain device stream channel", 3u),
                             UintIs("count of failures to start a device", 4u)})))));
}

// Tests methods that add and remove devices.
TEST_F(ReporterTest, AddRemoveDevices) {
  std::vector<Reporter::Container<Reporter::OutputDevice, Reporter::kObjectsToCache>::Ptr> outputs;
  std::vector<Reporter::Container<Reporter::InputDevice, Reporter::kObjectsToCache>::Ptr> inputs;
  for (size_t k = 0; k < 5; k++) {
    outputs.push_back(under_test_.CreateOutputDevice(fxl::StringPrintf("output_device_%lu", k),
                                                     fxl::StringPrintf("output_thread_%lu", k)));
  }
  for (size_t k = 0; k < 5; k++) {
    inputs.push_back(under_test_.CreateInputDevice(fxl::StringPrintf("input_device_%lu", k),
                                                   fxl::StringPrintf("input_thread_%lu", k)));
  }

  EXPECT_THAT(GetHierarchyLazyValues(),
              ChildrenMatch(IsSupersetOf(
                  {AllOf(NodeMatches(NameMatches("output devices")),
                         ChildrenMatch(UnorderedElementsAre(
                             NodeAlive("output_device_0"), NodeAlive("output_device_1"),
                             NodeAlive("output_device_2"), NodeAlive("output_device_3"),
                             NodeAlive("output_device_4")))),
                   AllOf(NodeMatches(NameMatches("input devices")),
                         ChildrenMatch(UnorderedElementsAre(
                             NodeAlive("input_device_0"), NodeAlive("input_device_1"),
                             NodeAlive("input_device_2"), NodeAlive("input_device_3"),
                             NodeAlive("input_device_4"))))})));

  outputs[0].Drop();
  outputs[1].Drop();
  outputs[2].Drop();
  outputs[3].Drop();
  inputs[0].Drop();
  inputs[1].Drop();
  inputs[2].Drop();
  inputs[3].Drop();

  EXPECT_THAT(GetHierarchyLazyValues(),
              ChildrenMatch(
                  IsSupersetOf({AllOf(NodeMatches(NameMatches("output devices")),
                                      ChildrenMatch(UnorderedElementsAre(
                                          NodeDead("output_device_0"), NodeDead("output_device_1"),
                                          NodeDead("output_device_2"), NodeDead("output_device_3"),
                                          NodeAlive("output_device_4")))),
                                AllOf(NodeMatches(NameMatches("input devices")),
                                      ChildrenMatch(UnorderedElementsAre(
                                          NodeDead("input_device_0"), NodeDead("input_device_1"),
                                          NodeDead("input_device_2"), NodeDead("input_device_3"),
                                          NodeAlive("input_device_4"))))})));

  outputs[4].Drop();
  inputs[4].Drop();

  // Garbage collect [0].
  EXPECT_THAT(GetHierarchyLazyValues(),
              ChildrenMatch(IsSupersetOf(
                  {AllOf(NodeMatches(NameMatches("output devices")),
                         ChildrenMatch(UnorderedElementsAre(
                             NodeDead("output_device_1"), NodeDead("output_device_2"),
                             NodeDead("output_device_3"), NodeDead("output_device_4")))),
                   AllOf(NodeMatches(NameMatches("input devices")),
                         ChildrenMatch(UnorderedElementsAre(
                             NodeDead("input_device_1"), NodeDead("input_device_2"),
                             NodeDead("input_device_3"), NodeDead("input_device_4"))))})));
}

// Tests methods that change device metrics.
TEST_F(ReporterTest, DeviceMetrics) {
  auto output_device = under_test_.CreateOutputDevice("output_device", "output_thread");
  auto input_device = under_test_.CreateInputDevice("input_device", "input_thread");

  // Note: GetHierachy uses ReadFromVmo, which cannot read lazy values.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(AllOf(
                    ChildrenMatch(UnorderedElementsAre(
                        NodeMatches(AllOf(
                            NameMatches("driver"),
                            PropertyList(UnorderedElementsAre(
                                UintIs("external delay (ns)", 0),
                                UintIs("external delay + fifo delay (ns)", 0),
                                UintIs("fifo delay (ns)", 0), UintIs("fifo depth in frames", 0),
                                StringIs("name", "unknown"))))),
                        NodeMatches(AllOf(
                            NameMatches("format"),
                            PropertyList(UnorderedElementsAre(StringIs("sample format", "unknown"),
                                                              UintIs("channels", 0),
                                                              UintIs("frames per second", 0))))),
                        NodeMatches(AllOf(NameMatches("device underflows"),
                                          PropertyList(UnorderedElementsAre(
                                              UintIs("count", 0), UintIs("duration (ns)", 0),
                                              UintIs("session count", 0))))),
                        NodeMatches(AllOf(NameMatches("pipeline underflows"),
                                          PropertyList(UnorderedElementsAre(
                                              UintIs("count", 0), UintIs("duration (ns)", 0),
                                              UintIs("session count", 0))))))),
                    NodeMatches(
                        AllOf(NameMatches("output_device"),
                              PropertyList(UnorderedElementsAre(
                                  DoubleIs("gain db", 0.0), BoolIs("muted", false),
                                  BoolIs("agc supported", false), BoolIs("agc enabled", false),
                                  StringIs("mixer thread name", "output_thread"))))))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(
                    AllOf(NameMatches("input_device"),
                          PropertyList(UnorderedElementsAre(
                              DoubleIs("gain db", 0.0), BoolIs("muted", false),
                              BoolIs("agc supported", false), BoolIs("agc enabled", false),
                              StringIs("mixer thread name", "input_thread")))))))),
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(
                    AllOf(NameMatches("thermal state"),
                          PropertyList(UnorderedElementsAre(UintIs("num thermal states", 1))))),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(
                    AllOf(NameMatches("normal"),
                          Not(PropertyList(Contains(UintIs("total duration (ns)", 0))))))))),
          AllOf(
              NodeMatches(NameMatches("thermal state transitions")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(IsSupersetOf({BoolIs("active", true), StringIs("state", "normal")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0))))))))),
          AllOf(NodeMatches(NameMatches("volume controls")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("active usage policies"),
                                  PropertyList(UnorderedElementsAre(
                                      DoubleIs("none gain db", 0.0), DoubleIs("duck gain db", 0.0),
                                      DoubleIs("mute gain db", 0.0))))),
                ChildrenMatch(Contains(NodeMatches(
                    AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", true)))))))))));

  output_device->StartSession(zx::time(0));
  output_device->DeviceUnderflow(zx::time(10), zx::time(15));
  output_device->DeviceUnderflow(zx::time(25), zx::time(30));
  output_device->StopSession(zx::time(50));
  output_device->StartSession(zx::time(90));
  output_device->DeviceUnderflow(zx::time(91), zx::time(92));
  output_device->PipelineUnderflow(zx::time(93), zx::time(96));
  output_device->StopSession(zx::time(100));

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(Contains(ChildrenMatch(IsSupersetOf({
              NodeMatches(AllOf(
                  NameMatches("device underflows"),
                  PropertyList(UnorderedElementsAre(UintIs("count", 3), UintIs("duration (ns)", 11),
                                                    UintIs("session count", 2))))),
              NodeMatches(AllOf(
                  NameMatches("pipeline underflows"),
                  PropertyList(UnorderedElementsAre(UintIs("count", 1), UintIs("duration (ns)", 3),
                                                    UintIs("session count", 2))))),
          }))))))));
}

// Tests method Device::SetGainInfo.
TEST_F(ReporterTest, DeviceSetGainInfo) {
  auto output_device = under_test_.CreateOutputDevice("output_device", "output_thread");

  // Expect initial device metric values.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                    NameMatches("output_device"),
                    PropertyList(IsSupersetOf({DoubleIs("gain db", 0.0), BoolIs("muted", false),
                                               BoolIs("agc supported", false),
                                               BoolIs("agc enabled", false)}))))))),
          AllOf(NodeMatches(NameMatches("input devices")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(
                    AllOf(NameMatches("thermal state"),
                          PropertyList(UnorderedElementsAre(UintIs("num thermal states", 1))))),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(
                    AllOf(NameMatches("normal"),
                          Not(PropertyList(Contains(UintIs("total duration (ns)", 0))))))))),
          AllOf(
              NodeMatches(NameMatches("thermal state transitions")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(IsSupersetOf({BoolIs("active", true), StringIs("state", "normal")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0))))))))),
          AllOf(NodeMatches(NameMatches("volume controls")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(AllOf(NameMatches("active usage policies"),
                                  PropertyList(UnorderedElementsAre(
                                      DoubleIs("none gain db", 0.0), DoubleIs("duck gain db", 0.0),
                                      DoubleIs("mute gain db", 0.0))))),
                ChildrenMatch(Contains(NodeMatches(
                    AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", true)))))))))));

  fuchsia::media::AudioGainInfo gain_info_a{
      .gain_db = -1.0f,
      .flags = fuchsia::media::AudioGainInfoFlags::MUTE |
               fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED |
               fuchsia::media::AudioGainInfoFlags::AGC_ENABLED};

  output_device->SetGainInfo(gain_info_a, {});

  // Expect initial device metric values.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(IsSupersetOf({DoubleIs("gain db", 0.0), BoolIs("muted", false),
                                                 BoolIs("agc supported", false),
                                                 BoolIs("agc enabled", false)}))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::GAIN_VALID);

  // Expect a gain change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(IsSupersetOf({DoubleIs("gain db", -1.0), BoolIs("muted", false),
                                                 BoolIs("agc supported", false),
                                                 BoolIs("agc enabled", false)}))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::MUTE_VALID);

  // Expect a mute change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(IsSupersetOf({DoubleIs("gain db", -1.0), BoolIs("muted", true),
                                                 BoolIs("agc supported", false),
                                                 BoolIs("agc enabled", false)}))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::AGC_VALID);

  // Expect an agc change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(IsSupersetOf({DoubleIs("gain db", -1.0), BoolIs("muted", true),
                                                 BoolIs("agc supported", true),
                                                 BoolIs("agc enabled", true)}))))))))));

  fuchsia::media::AudioGainInfo gain_info_b{.gain_db = -2.0f, .flags = {}};
  output_device->SetGainInfo(gain_info_b, fuchsia::media::AudioGainValidFlags::GAIN_VALID |
                                              fuchsia::media::AudioGainValidFlags::MUTE_VALID |
                                              fuchsia::media::AudioGainValidFlags::AGC_VALID);

  // Expect all changes.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(IsSupersetOf({DoubleIs("gain db", -2.0), BoolIs("muted", false),
                                                 BoolIs("agc supported", false),
                                                 BoolIs("agc enabled", false)}))))))))));
}

// Tests methods that add and remove client ports.
TEST_F(ReporterTest, AddRemoveClientPorts) {
  std::vector<Reporter::Container<Reporter::Renderer, Reporter::kObjectsToCache>::Ptr> renderers;
  std::vector<Reporter::Container<Reporter::Capturer, Reporter::kObjectsToCache>::Ptr> capturers;
  for (size_t k = 0; k < 5; k++) {
    renderers.push_back(under_test_.CreateRenderer());
  }
  for (size_t k = 0; k < 5; k++) {
    capturers.push_back(under_test_.CreateCapturer(fxl::StringPrintf("capture_thread_%lu", k)));
  }

  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(IsSupersetOf(
          {AllOf(NodeMatches(NameMatches("renderers")),
                 ChildrenMatch(UnorderedElementsAre(NodeAlive("1"), NodeAlive("2"), NodeAlive("3"),
                                                    NodeAlive("4"), NodeAlive("5")))),
           AllOf(NodeMatches(NameMatches("capturers")),
                 ChildrenMatch(UnorderedElementsAre(NodeAlive("1"), NodeAlive("2"), NodeAlive("3"),
                                                    NodeAlive("4"), NodeAlive("5"))))})));

  renderers[0].Drop();
  renderers[1].Drop();
  renderers[2].Drop();
  renderers[3].Drop();
  capturers[0].Drop();
  capturers[1].Drop();
  capturers[2].Drop();
  capturers[3].Drop();

  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(IsSupersetOf(
          {AllOf(NodeMatches(NameMatches("renderers")),
                 ChildrenMatch(UnorderedElementsAre(NodeDead("1"), NodeDead("2"), NodeDead("3"),
                                                    NodeDead("4"), NodeAlive("5")))),
           AllOf(NodeMatches(NameMatches("capturers")),
                 ChildrenMatch(UnorderedElementsAre(NodeDead("1"), NodeDead("2"), NodeDead("3"),
                                                    NodeDead("4"), NodeAlive("5"))))})));

  renderers[4].Drop();
  capturers[4].Drop();

  // Garbage collect [0].
  EXPECT_THAT(GetHierarchyLazyValues(),
              ChildrenMatch(IsSupersetOf(
                  {AllOf(NodeMatches(NameMatches("renderers")),
                         ChildrenMatch(UnorderedElementsAre(NodeDead("2"), NodeDead("3"),
                                                            NodeDead("4"), NodeDead("5")))),
                   AllOf(NodeMatches(NameMatches("capturers")),
                         ChildrenMatch(UnorderedElementsAre(NodeDead("2"), NodeDead("3"),
                                                            NodeDead("4"), NodeDead("5"))))})));
}

// Tests methods that change renderer metrics.
TEST_F(ReporterTest, RendererMetrics) {
  auto renderer = under_test_.CreateRenderer();

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("renderers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(UnorderedElementsAre(
                  NodeMatches(AllOf(NameMatches("underflows"),
                                    PropertyList(UnorderedElementsAre(
                                        UintIs("count", 0), UintIs("duration (ns)", 0),
                                        UintIs("session count", 0))))),
                  NodeMatches(AllOf(NameMatches("format"),
                                    PropertyList(UnorderedElementsAre(
                                        StringIs("sample format", "unknown"), UintIs("channels", 0),
                                        UintIs("frames per second", 0))))),
                  AllOf(NodeMatches(NameMatches("payload buffers")), ChildrenMatch(IsEmpty())))),
              NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(UnorderedElementsAre(
                      DoubleIs("gain db", 0.0), BoolIs("muted", false),
                      UintIs("calls to SetGainWithRamp", 0), UintIs("min lead time (ns)", 0),
                      DoubleIs("pts continuity threshold (s)", 0.0),
                      UintIs("pts units denominator", 1), UintIs("pts units numerator", 1000000000),
                      DoubleIs("final stream gain (post-volume) dbfs", 0),
                      StringIs("usage", "default"))))))))))));

  renderer->SetUsage(RenderUsage::MEDIA);
  renderer->SetFormat(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value());
  renderer->AddPayloadBuffer(0, 4096);
  renderer->AddPayloadBuffer(10, 8192);
  renderer->SendPacket(fuchsia::media::StreamPacket{
      .payload_buffer_id = 10,
  });
  renderer->SetGain(-1.0);
  renderer->SetGainWithRamp(-1.0, zx::sec(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  renderer->SetGainWithRamp(-1.0, zx::sec(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  renderer->SetMute(true);
  renderer->SetMinLeadTime(zx::nsec(1000000));
  renderer->SetPtsContinuityThreshold(5.0);
  renderer->SetPtsUnits(1234567, 3);
  renderer->SetFinalGain(-6.0);

  renderer->StartSession(zx::time(0));
  renderer->Underflow(zx::time(10), zx::time(15));
  renderer->StopSession(zx::time(100));

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("renderers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(UnorderedElementsAre(
                  NodeMatches(AllOf(NameMatches("underflows"),
                                    PropertyList(UnorderedElementsAre(
                                        UintIs("count", 1), UintIs("duration (ns)", 5),
                                        UintIs("session count", 1))))),
                  NodeMatches(AllOf(
                      NameMatches("format"),
                      PropertyList(UnorderedElementsAre(StringIs("sample format", "SIGNED_16"),
                                                        UintIs("channels", 2),
                                                        UintIs("frames per second", 48000))))),
                  AllOf(NodeMatches(NameMatches("payload buffers")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(AllOf(NameMatches("0"),
                                              PropertyList(UnorderedElementsAre(
                                                  UintIs("size", 4096), UintIs("packets", 0))))),
                            NodeMatches(AllOf(NameMatches("10"), PropertyList(UnorderedElementsAre(
                                                                     UintIs("size", 8192),
                                                                     UintIs("packets", 1)))))))))),
              NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(UnorderedElementsAre(
                      DoubleIs("gain db", -1.0), BoolIs("muted", true),
                      UintIs("calls to SetGainWithRamp", 2), UintIs("min lead time (ns)", 1000000),
                      DoubleIs("pts continuity threshold (s)", 5.0),
                      UintIs("pts units denominator", 3), UintIs("pts units numerator", 1234567),
                      DoubleIs("final stream gain (post-volume) dbfs", -6.0),
                      StringIs("usage", "RenderUsage::MEDIA"))))))))))));
}

// Tests methods that change capturer metrics.
TEST_F(ReporterTest, CapturerMetrics) {
  auto capturer = under_test_.CreateCapturer("thread");

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(UnorderedElementsAre(
                  NodeMatches(AllOf(NameMatches("overflows"),
                                    PropertyList(UnorderedElementsAre(
                                        UintIs("count", 0), UintIs("duration (ns)", 0),
                                        UintIs("session count", 0))))),
                  NodeMatches(AllOf(NameMatches("format"),
                                    PropertyList(UnorderedElementsAre(
                                        StringIs("sample format", "unknown"), UintIs("channels", 0),
                                        UintIs("frames per second", 0))))),
                  AllOf(NodeMatches(NameMatches("payload buffers")), ChildrenMatch(IsEmpty())))),
              NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(UnorderedElementsAre(
                      DoubleIs("gain db", 0.0), BoolIs("muted", false),
                      UintIs("min fence time (ns)", 0), UintIs("calls to SetGainWithRamp", 0),
                      StringIs("usage", "default"),
                      StringIs("mixer thread name", "thread"))))))))))));

  capturer->SetUsage(CaptureUsage::FOREGROUND);
  capturer->SetFormat(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value());
  capturer->AddPayloadBuffer(0, 4096);
  capturer->AddPayloadBuffer(10, 8192);
  capturer->SendPacket(fuchsia::media::StreamPacket{
      .payload_buffer_id = 10,
  });
  capturer->SetGain(-1.0);
  capturer->SetGainWithRamp(-1.0, zx::sec(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  capturer->SetGainWithRamp(-1.0, zx::sec(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  capturer->SetMute(true);
  capturer->SetMinFenceTime(zx::nsec(2'000'000));

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(UnorderedElementsAre(
                  NodeMatches(AllOf(NameMatches("overflows"),
                                    PropertyList(UnorderedElementsAre(
                                        UintIs("count", 0), UintIs("duration (ns)", 0),
                                        UintIs("session count", 0))))),
                  NodeMatches(AllOf(
                      NameMatches("format"),
                      PropertyList(UnorderedElementsAre(StringIs("sample format", "SIGNED_16"),
                                                        UintIs("channels", 2),
                                                        UintIs("frames per second", 48000))))),
                  AllOf(NodeMatches(NameMatches("payload buffers")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(AllOf(NameMatches("0"),
                                              PropertyList(UnorderedElementsAre(
                                                  UintIs("size", 4096), UintIs("packets", 0))))),
                            NodeMatches(AllOf(NameMatches("10"), PropertyList(UnorderedElementsAre(
                                                                     UintIs("size", 8192),
                                                                     UintIs("packets", 1)))))))))),
              NodeMatches(
                  AllOf(NameMatches("1"), PropertyList(UnorderedElementsAre(
                                              DoubleIs("gain db", -1.0), BoolIs("muted", true),
                                              UintIs("min fence time (ns)", 2'000'000),
                                              UintIs("calls to SetGainWithRamp", 2),
                                              StringIs("usage", "CaptureUsage::FOREGROUND"),
                                              StringIs("mixer thread name", "thread"))))))))))));
}

// Tests ThermalStateTracker methods.
TEST_F(ReporterTest, SetThermalStateMetrics) {
  under_test_.SetNumThermalStates(3);
  under_test_.SetThermalState(0);
  // Expect first thermal state metric values.
  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("thermal state"),
                            PropertyList(UnorderedElementsAre(UintIs("num thermal states", 3))))),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(
              AllOf(NameMatches("normal"),
                    Not(PropertyList(Contains(UintIs("total duration (ns)", 0))))))))))));
  // Expect second thermal state metric values, with first thermal state metrics stored.
  under_test_.SetThermalState(2);
  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("thermal state"),
                            PropertyList(UnorderedElementsAre(UintIs("num thermal states", 3))))),
          ChildrenMatch(UnorderedElementsAre(
              NodeMatches(AllOf(NameMatches("normal"),
                                Not(PropertyList(Contains(UintIs("total duration (ns)", 0)))))),
              NodeMatches(AllOf(NameMatches("2"), Not(PropertyList(Contains(
                                                      UintIs("total duration (ns)", 0))))))))))));
  // Expect values to be unchanged, since state 2 has already been triggered.
  under_test_.SetThermalState(2);
  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("thermal state"),
                            PropertyList(UnorderedElementsAre(UintIs("num thermal states", 3))))),
          ChildrenMatch(UnorderedElementsAre(
              NodeMatches(AllOf(NameMatches("normal"),
                                Not(PropertyList(Contains(UintIs("total duration (ns)", 0)))))),
              NodeMatches(AllOf(NameMatches("2"), Not(PropertyList(Contains(
                                                      UintIs("total duration (ns)", 0))))))))))));
}

// Tests caching of ThermalStates up to limit Reporter::kThermalStatesToCache == 8.
TEST_F(ReporterTest, CacheThermalStateTransitions) {
  // Reporter initializes thermal state to 0.
  under_test_.SetThermalState(1);  // ThermalState 2, first cached
  under_test_.SetThermalState(2);
  under_test_.SetThermalState(0);
  under_test_.SetThermalState(1);
  under_test_.SetThermalState(2);
  under_test_.SetThermalState(1);
  under_test_.SetThermalState(2);
  under_test_.SetThermalState(2);  // Skip duplicate.
  under_test_.SetThermalState(0);  // ThermalState 9, final cached
  under_test_.SetThermalState(1);  // ThermalState 10, alive

  // Expect most recent 8 thermal state metric values.
  EXPECT_THAT(
      GetHierarchyLazyValues(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("thermal state transitions")),
          ChildrenMatch(UnorderedElementsAre(
              NodeMatches(AllOf(
                  NameMatches("2"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "1")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("3"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "2")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(NameMatches("4"),
                                PropertyList(IsSupersetOf(
                                    {BoolIs("active", false), StringIs("state", "normal")})),
                                Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("5"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "1")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("6"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "2")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("7"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "1")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("8"),
                  PropertyList(IsSupersetOf({BoolIs("active", false), StringIs("state", "2")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(NameMatches("9"),
                                PropertyList(IsSupersetOf(
                                    {BoolIs("active", false), StringIs("state", "normal")})),
                                Not(PropertyList(Contains(UintIs("duration (ns)", 0)))))),
              NodeMatches(AllOf(
                  NameMatches("10"),
                  PropertyList(IsSupersetOf({BoolIs("active", true), StringIs("state", "1")})),
                  Not(PropertyList(Contains(UintIs("duration (ns)", 0))))))))))));
}

// Tests VolumeControl methods.
TEST_F(ReporterTest, VolumeControlMetrics) {
  auto volume_control = under_test_.CreateVolumeControl();

  // Expect initial volume control metrics.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("volume controls")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(AllOf(NameMatches("1"), PropertyList(UnorderedElementsAre(
                                                      UintIs("client count", 0),
                                                      StringIs("name", "unknown - no clients"))))),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("volume settings")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("1"), PropertyList(UnorderedElementsAre(
                                                  BoolIs("active", true), BoolIs("mute", false),
                                                  DoubleIs("volume", 0.0)))))))))))))))));

  volume_control->SetVolumeMute(0.5, true);
  volume_control->AddBinding("RenderUsage::MEDIA");
  volume_control->AddBinding("RenderUsage::MEDIA");

  // Expect |volume_control| settings to be reflected, with past volume settings cached.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("volume controls")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(AllOf(NameMatches("1"), PropertyList(UnorderedElementsAre(
                                                      UintIs("client count", 2),
                                                      StringIs("name", "RenderUsage::MEDIA"))))),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("volume settings")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(AllOf(NameMatches("1"),
                                              PropertyList(UnorderedElementsAre(
                                                  BoolIs("active", false), BoolIs("mute", false),
                                                  DoubleIs("volume", 0.0))))),
                            NodeMatches(AllOf(NameMatches("2"),
                                              PropertyList(UnorderedElementsAre(
                                                  BoolIs("active", true), BoolIs("mute", true),
                                                  DoubleIs("volume", 0.5)))))))))))))))));
}

// Tests methods that change audio policy metrics.
TEST_F(ReporterTest, AudioPolicyMetrics) {
  // Expect behavior gains to be logged, and initial active audio policy to have no active usages.
  under_test_.SetAudioPolicyBehaviorGain(
      AudioAdmin::BehaviorGain({.none_gain_db = 0., .duck_gain_db = -10., .mute_gain_db = -100.}));
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("active usage policies"),
                            PropertyList(UnorderedElementsAre(DoubleIs("none gain db", 0.0),
                                                              DoubleIs("duck gain db", -10.0),
                                                              DoubleIs("mute gain db", -100.0))))),
          ChildrenMatch(Contains(NodeMatches(
              AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", true)))))))))));

  // Structures to hold active usages and usage behaviors.
  std::vector<fuchsia::media::Usage> active_usages;
  std::array<fuchsia::media::Behavior, fuchsia::media::RENDER_USAGE_COUNT> render_usage_behaviors;
  std::array<fuchsia::media::Behavior, fuchsia::media::CAPTURE_USAGE_COUNT> capture_usage_behaviors;
  render_usage_behaviors.fill(fuchsia::media::Behavior::NONE);
  capture_usage_behaviors.fill(fuchsia::media::Behavior::NONE);

  // Expect active RenderUsage::MEDIA to be logged, with default policy NONE.
  active_usages.push_back(
      fuchsia::media::Usage::WithRenderUsage((fuchsia::media::AudioRenderUsage::MEDIA)));
  under_test_.UpdateActiveUsagePolicy(active_usages, render_usage_behaviors,
                                      capture_usage_behaviors);
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("active usage policies"),
                            PropertyList(UnorderedElementsAre(DoubleIs("none gain db", 0.0),
                                                              DoubleIs("duck gain db", -10.0),
                                                              DoubleIs("mute gain db", -100.0))))),
          ChildrenMatch(UnorderedElementsAre(
              NodeMatches(AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", false))))),
              NodeMatches(AllOf(
                  NameMatches("2"),
                  PropertyList(UnorderedElementsAre(
                      BoolIs("active", true), StringIs("RenderUsage::MEDIA", "NONE")))))))))));

  // Expect active RenderUsage::MEDIA and CaptureUsage::SYSTEM_AGENT to be logged, with DUCK applied
  // to MEDIA.
  active_usages.push_back(
      fuchsia::media::Usage::WithCaptureUsage((fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)));
  render_usage_behaviors[static_cast<int>(fuchsia::media::AudioRenderUsage::MEDIA)] =
      fuchsia::media::Behavior::DUCK;
  under_test_.UpdateActiveUsagePolicy(active_usages, render_usage_behaviors,
                                      capture_usage_behaviors);
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(NameMatches("active usage policies"),
                            PropertyList(UnorderedElementsAre(DoubleIs("none gain db", 0.0),
                                                              DoubleIs("duck gain db", -10.0),
                                                              DoubleIs("mute gain db", -100.0))))),
          ChildrenMatch(UnorderedElementsAre(
              NodeMatches(AllOf(NameMatches("1"), PropertyList(Contains(BoolIs("active", false))))),
              NodeMatches(AllOf(NameMatches("2"), PropertyList(UnorderedElementsAre(
                                                      BoolIs("active", false),
                                                      StringIs("RenderUsage::MEDIA", "NONE"))))),
              NodeMatches(AllOf(NameMatches("3"),
                                PropertyList(UnorderedElementsAre(
                                    BoolIs("active", true), StringIs("RenderUsage::MEDIA", "DUCK"),
                                    StringIs("CaptureUsage::SYSTEM_AGENT", "NONE")))))))))));
}
}  // namespace
}  // namespace media::audio
