// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/inspect/testing/cpp/inspect.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/test/null_audio_capturer.h"
#include "src/media/audio/lib/test/null_audio_renderer.h"

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
  return NodeMatches(AllOf(NameMatches(name), PropertyList(Contains(BoolIs("alive", true)))));
}

::testing::Matcher<const ::inspect::Hierarchy&> NodeDead(const std::string& name) {
  return NodeMatches(AllOf(NameMatches(name), PropertyList(Contains(BoolIs("alive", false)))));
}

class ReporterTest : public testing::ThreadingModelFixture {
 public:
  ReporterTest() : under_test_(context().component_context(), threading_model()) {}

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

  Reporter under_test_;
};

// Tests reporter initial state.
TEST_F(ReporterTest, InitialState) {
  auto hierarchy = GetHierarchy();

  // Expect metrics with default values in the root node.
  EXPECT_THAT(
      hierarchy,
      NodeMatches(AllOf(NameMatches("root"),
                        PropertyList(UnorderedElementsAre(
                            UintIs("count of failures to open device", 0),
                            UintIs("count of failures to obtain device fdio service channel", 0),
                            UintIs("count of failures to obtain device stream channel", 0),
                            UintIs("count of failures to start a device", 0))))));

  // Expect empty child nodes for devices and client ports.
  EXPECT_THAT(hierarchy,
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
                        ChildrenMatch(IsEmpty())))));
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
                        PropertyList(UnorderedElementsAre(
                            UintIs("count of failures to open device", 1u),
                            UintIs("count of failures to obtain device fdio service channel", 2u),
                            UintIs("count of failures to obtain device stream channel", 3u),
                            UintIs("count of failures to start a device", 4u))))));
}

// Tests methods that add and remove devices.
TEST_F(ReporterTest, AddRemoveDevices) {
  std::vector<Reporter::Container<Reporter::OutputDevice>::Ptr> outputs;
  std::vector<Reporter::Container<Reporter::InputDevice>::Ptr> inputs;
  for (size_t k = 0; k < 5; k++) {
    outputs.push_back(under_test_.CreateOutputDevice(fxl::StringPrintf("output_device_%lu", k)));
  }
  for (size_t k = 0; k < 5; k++) {
    inputs.push_back(under_test_.CreateInputDevice(fxl::StringPrintf("input_device_%lu", k)));
  }

  EXPECT_THAT(GetHierarchy(),
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

  EXPECT_THAT(GetHierarchy(),
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
  EXPECT_THAT(GetHierarchy(),
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
  auto output_device = under_test_.CreateOutputDevice("output_device");
  auto input_device = under_test_.CreateInputDevice("input_device");

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
                    NodeMatches(AllOf(
                        NameMatches("output_device"),
                        PropertyList(UnorderedElementsAre(
                            BoolIs("alive", true), DoubleIs("gain db", 0.0), BoolIs("muted", false),
                            BoolIs("agc supported", false), BoolIs("agc enabled", false))))))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                    NameMatches("input_device"),
                    PropertyList(UnorderedElementsAre(
                        BoolIs("alive", true), DoubleIs("gain db", 0.0), BoolIs("muted", false),
                        BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))),
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())))));

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
  auto output_device = under_test_.CreateOutputDevice("output_device");

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
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())))));

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
  std::vector<Reporter::Container<Reporter::Renderer>::Ptr> renderers;
  std::vector<Reporter::Container<Reporter::Capturer>::Ptr> capturers;
  for (size_t k = 0; k < 5; k++) {
    renderers.push_back(under_test_.CreateRenderer());
  }
  for (size_t k = 0; k < 5; k++) {
    capturers.push_back(under_test_.CreateCapturer());
  }

  EXPECT_THAT(
      GetHierarchy(),
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
      GetHierarchy(),
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
  EXPECT_THAT(GetHierarchy(),
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
              NodeMatches(
                  AllOf(NameMatches("1"),
                        PropertyList(UnorderedElementsAre(
                            BoolIs("alive", true), DoubleIs("gain db", 0.0), BoolIs("muted", false),
                            UintIs("calls to SetGainWithRamp", 0), UintIs("min lead time (ns)", 0),
                            DoubleIs("pts continuity threshold (s)", 0.0),
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
                      BoolIs("alive", true), DoubleIs("gain db", -1.0), BoolIs("muted", true),
                      UintIs("calls to SetGainWithRamp", 2), UintIs("min lead time (ns)", 1000000),
                      DoubleIs("pts continuity threshold (s)", 5.0),
                      DoubleIs("final stream gain (post-volume) dbfs", -6.0),
                      StringIs("usage", "RenderUsage::MEDIA"))))))))))));
}

// Tests methods that change capturer metrics.
TEST_F(ReporterTest, CapturerMetrics) {
  auto capturer = under_test_.CreateCapturer();

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
              NodeMatches(
                  AllOf(NameMatches("1"),
                        PropertyList(UnorderedElementsAre(
                            BoolIs("alive", true), DoubleIs("gain db", 0.0), BoolIs("muted", false),
                            UintIs("min fence time (ns)", 0), UintIs("calls to SetGainWithRamp", 0),
                            StringIs("usage", "default"))))))))))));

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
              NodeMatches(AllOf(NameMatches("1"),
                                PropertyList(UnorderedElementsAre(
                                    BoolIs("alive", true), DoubleIs("gain db", -1.0),
                                    BoolIs("muted", true), UintIs("min fence time (ns)", 2'000'000),
                                    UintIs("calls to SetGainWithRamp", 2),
                                    StringIs("usage", "CaptureUsage::FOREGROUND"))))))))))));
}

}  // namespace
}  // namespace media::audio
