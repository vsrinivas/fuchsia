// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/inspect/testing/cpp/inspect.h>

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
using ::inspect::testing::UintIs;
using ::testing::AllOf;
using ::testing::IsEmpty;

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
  auto output_device_a = under_test_.CreateOutputDevice("output_device_a");
  auto output_device_b = under_test_.CreateOutputDevice("output_device_b");
  auto input_device_a = under_test_.CreateInputDevice("input_device_a");
  auto input_device_b = under_test_.CreateInputDevice("input_device_b");

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("output_device_a")),
                                                   NodeMatches(NameMatches("output_device_b"))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("input_device_a")),
                                                   NodeMatches(NameMatches("input_device_b"))))),
          NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));

  output_device_a.reset(nullptr);
  input_device_b.reset(nullptr);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("output_device_b"))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("input_device_a"))))),
          NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));

  output_device_a = under_test_.CreateOutputDevice("output_device_a");
  input_device_b = under_test_.CreateInputDevice("input_device_b");

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("output_device_a")),
                                                   NodeMatches(NameMatches("output_device_b"))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("input_device_a")),
                                                   NodeMatches(NameMatches("input_device_b"))))),
          NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));

  output_device_a.reset(nullptr);
  output_device_b.reset(nullptr);
  input_device_a.reset(nullptr);
  input_device_b.reset(nullptr);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")), ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(NameMatches("input devices")), ChildrenMatch(IsEmpty())),
                  NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));
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
                            DoubleIs("gain db", 0.0), BoolIs("muted", false),
                            BoolIs("agc supported", false), BoolIs("agc enabled", false))))))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(
                    AllOf(NameMatches("input_device"),
                          PropertyList(UnorderedElementsAre(
                              DoubleIs("gain db", 0.0), BoolIs("muted", false),
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

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(AllOf(
                      ChildrenMatch(UnorderedElementsAre(
                          NodeMatches(AllOf(NameMatches("device underflows"),
                                            PropertyList(UnorderedElementsAre(
                                                UintIs("count", 3), UintIs("duration (ns)", 11),
                                                UintIs("session count", 2))))),
                          NodeMatches(AllOf(NameMatches("pipeline underflows"),
                                            PropertyList(UnorderedElementsAre(
                                                UintIs("count", 1), UintIs("duration (ns)", 3),
                                                UintIs("session count", 2))))))),
                      NodeMatches(AllOf(NameMatches("output_device"),
                                        PropertyList(UnorderedElementsAre(
                                            DoubleIs("gain db", 0.0), BoolIs("muted", false),
                                            BoolIs("agc supported", false),
                                            BoolIs("agc enabled", false))))))))))));
}

// Tests method Device::SetGainInfo.
TEST_F(ReporterTest, DeviceSetGainInfo) {
  auto output_device = under_test_.CreateOutputDevice("output_device");

  // Expect initial device metric values.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", 0.0), BoolIs("muted", false),
                                BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))),
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
                      PropertyList(UnorderedElementsAre(
                          DoubleIs("gain db", 0.0), BoolIs("muted", false),
                          BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::GAIN_VALID);

  // Expect a gain change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(UnorderedElementsAre(
                          DoubleIs("gain db", -1.0), BoolIs("muted", false),
                          BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::MUTE_VALID);

  // Expect a mute change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(UnorderedElementsAre(
                          DoubleIs("gain db", -1.0), BoolIs("muted", true),
                          BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))))));

  output_device->SetGainInfo(gain_info_a, fuchsia::media::AudioGainValidFlags::AGC_VALID);

  // Expect an agc change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("output devices")),
                  ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                      NameMatches("output_device"),
                      PropertyList(UnorderedElementsAre(
                          DoubleIs("gain db", -1.0), BoolIs("muted", true),
                          BoolIs("agc supported", true), BoolIs("agc enabled", true)))))))))));

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
                      PropertyList(UnorderedElementsAre(
                          DoubleIs("gain db", -2.0), BoolIs("muted", false),
                          BoolIs("agc supported", false), BoolIs("agc enabled", false)))))))))));
}

// Tests methods that add and remove client ports.
TEST_F(ReporterTest, AddRemoveClientPorts) {
  auto renderer_a = under_test_.CreateRenderer();
  auto renderer_b = under_test_.CreateRenderer();
  auto capturer_a = under_test_.CreateCapturer();
  auto capturer_b = under_test_.CreateCapturer();

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("1")),
                                                   NodeMatches(NameMatches("2"))))),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("1")),
                                                   NodeMatches(NameMatches("2"))))),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));

  renderer_a.reset(nullptr);
  capturer_b.reset(nullptr);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("2"))))),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("1"))))),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));

  renderer_a = under_test_.CreateRenderer();
  capturer_b = under_test_.CreateCapturer();

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("3")),
                                                   NodeMatches(NameMatches("2"))))),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("1")),
                                                   NodeMatches(NameMatches("3"))))),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));

  renderer_a.reset(nullptr);
  renderer_b.reset(nullptr);
  capturer_a.reset(nullptr);
  capturer_b.reset(nullptr);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));
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
                  AllOf(NodeMatches(NameMatches("payload buffers")), ChildrenMatch(IsEmpty())))),
              NodeMatches(AllOf(NameMatches("1"),
                                PropertyList(UnorderedElementsAre(
                                    UintIs("sample format", 0), UintIs("channels", 0),
                                    UintIs("frames per second", 0), DoubleIs("gain db", 0.0),
                                    BoolIs("muted", false), UintIs("calls to SetGainWithRamp", 0),
                                    UintIs("min lead time (ns)", 0),
                                    DoubleIs("pts continuity threshold (s)", 0.0),
                                    DoubleIs("final stream gain (post-volume) dbfs", 0))))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  renderer->SetStreamType(stream_type);
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
                      UintIs("sample format", static_cast<uint64_t>(stream_type.sample_format)),
                      UintIs("channels", stream_type.channels),
                      UintIs("frames per second", stream_type.frames_per_second),
                      DoubleIs("gain db", -1.0), BoolIs("muted", true),
                      UintIs("calls to SetGainWithRamp", 2), UintIs("min lead time (ns)", 1000000),
                      DoubleIs("pts continuity threshold (s)", 5.0),
                      DoubleIs("final stream gain (post-volume) dbfs", -6.0))))))))))));
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
                  AllOf(NodeMatches(NameMatches("payload buffers")), ChildrenMatch(IsEmpty())))),
              NodeMatches(AllOf(NameMatches("1"),
                                PropertyList(UnorderedElementsAre(
                                    UintIs("sample format", 0), UintIs("channels", 0),
                                    UintIs("frames per second", 0), DoubleIs("gain db", 0.0),
                                    BoolIs("muted", false), UintIs("min fence time (ns)", 0),
                                    UintIs("calls to SetGainWithRamp", 0))))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  capturer->SetStreamType(stream_type);
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
                      UintIs("sample format", static_cast<uint64_t>(stream_type.sample_format)),
                      UintIs("channels", stream_type.channels),
                      UintIs("frames per second", stream_type.frames_per_second),
                      DoubleIs("gain db", -1.0), BoolIs("muted", true),
                      UintIs("min fence time (ns)", 2'000'000),
                      UintIs("calls to SetGainWithRamp", 2))))))))))));
}

}  // namespace
}  // namespace media::audio
