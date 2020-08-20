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
  testing::FakeAudioInput device(&threading_model(), &context().device_manager(),
                                 &context().link_matrix());
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);

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
  testing::FakeAudioOutput output_device_a(&threading_model(), &context().device_manager(),
                                           &context().link_matrix());
  testing::FakeAudioOutput output_device_b(&threading_model(), &context().device_manager(),
                                           &context().link_matrix());
  testing::FakeAudioInput input_device_a(&threading_model(), &context().device_manager(),
                                         &context().link_matrix());
  testing::FakeAudioInput input_device_b(&threading_model(), &context().device_manager(),
                                         &context().link_matrix());

  under_test_.AddingDevice("output_device_a", output_device_a);
  under_test_.AddingDevice("output_device_b", output_device_b);
  under_test_.AddingDevice("input_device_a", input_device_a);
  under_test_.AddingDevice("input_device_b", input_device_b);

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

  under_test_.RemovingDevice(output_device_a);
  under_test_.RemovingDevice(input_device_b);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("output_device_b"))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("input_device_a"))))),
          NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));

  under_test_.AddingDevice("output_device_a", output_device_a);
  under_test_.AddingDevice("input_device_b", input_device_b);

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

  under_test_.RemovingDevice(output_device_a);
  under_test_.RemovingDevice(output_device_b);
  under_test_.RemovingDevice(input_device_a);
  under_test_.RemovingDevice(input_device_b);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")), ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(NameMatches("input devices")), ChildrenMatch(IsEmpty())),
                  NodeMatches(NameMatches("renderers")), NodeMatches(NameMatches("capturers")))));
}

// Tests methods that change device metrics.
TEST_F(ReporterTest, DeviceMetrics) {
  testing::FakeAudioOutput output_device(&threading_model(), &context().device_manager(),
                                         &context().link_matrix());
  testing::FakeAudioInput input_device(&threading_model(), &context().device_manager(),
                                       &context().link_matrix());

  under_test_.AddingDevice("output_device", output_device);
  under_test_.AddingDevice("input_device", input_device);

  // Note: GetHierachy uses ReadFromVmo, which cannot read lazy values.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("output devices")),
                ChildrenMatch(UnorderedElementsAre(
                    AllOf(ChildrenMatch(UnorderedElementsAre(
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
                                  DoubleIs("gain db", 0.0), UintIs("muted", 0),
                                  UintIs("agc supported", 0), UintIs("agc enabled", 0))))))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                    NameMatches("input_device"),
                    PropertyList(UnorderedElementsAre(DoubleIs("gain db", 0.0), UintIs("muted", 0),
                                                      UintIs("agc supported", 0),
                                                      UintIs("agc enabled", 0)))))))),
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())))));

  under_test_.OutputDeviceStartSession(output_device, zx::time(0));
  under_test_.OutputDeviceUnderflow(output_device, zx::time(10), zx::time(15));
  under_test_.OutputDeviceUnderflow(output_device, zx::time(25), zx::time(30));
  under_test_.OutputDeviceStopSession(output_device, zx::time(50));
  under_test_.OutputDeviceStartSession(output_device, zx::time(90));
  under_test_.OutputDeviceUnderflow(output_device, zx::time(91), zx::time(92));
  under_test_.OutputDeviceStopSession(output_device, zx::time(100));

  under_test_.OutputPipelineStartSession(output_device, zx::time(0));
  under_test_.OutputPipelineUnderflow(output_device, zx::time(90), zx::time(96));
  under_test_.OutputPipelineStopSession(output_device, zx::time(100));

  EXPECT_THAT(
      GetHierarchy(),
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
                                        UintIs("count", 1), UintIs("duration (ns)", 6),
                                        UintIs("session count", 1))))))),
              NodeMatches(AllOf(NameMatches("output_device"),
                                PropertyList(UnorderedElementsAre(
                                    DoubleIs("gain db", 0.0), UintIs("muted", 0),
                                    UintIs("agc supported", 0), UintIs("agc enabled", 0))))))))))));
}

// Tests method SettingDeviceGainInfo.
TEST_F(ReporterTest, SettingDeviceGainInfo) {
  testing::FakeAudioOutput output_device(&threading_model(), &context().device_manager(),
                                         &context().link_matrix());
  testing::FakeAudioInput input_device(&threading_model(), &context().device_manager(),
                                       &context().link_matrix());

  under_test_.AddingDevice("output_device", output_device);

  // Expect initial device metric values.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(
                            AllOf(NameMatches("output_device"),
                                  PropertyList(UnorderedElementsAre(
                                      DoubleIs("gain db", 0.0), UintIs("muted", 0),
                                      UintIs("agc supported", 0), UintIs("agc enabled", 0)))))))),
                  AllOf(NodeMatches(NameMatches("input devices")), ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())))));

  fuchsia::media::AudioGainInfo gain_info_a{
      .gain_db = -1.0f,
      .flags = fuchsia::media::AudioGainInfoFlags::MUTE |
               fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED |
               fuchsia::media::AudioGainInfoFlags::AGC_ENABLED};

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a, {});

  // Expect initial device metric values.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", 0.0), UintIs("muted", 0),
                                UintIs("agc supported", 0), UintIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::AudioGainValidFlags::GAIN_VALID);

  // Expect a gain change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", -1.0), UintIs("muted", 0),
                                UintIs("agc supported", 0), UintIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::AudioGainValidFlags::MUTE_VALID);

  // Expect a mute change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", -1.0), UintIs("muted", 1),
                                UintIs("agc supported", 0), UintIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::AudioGainValidFlags::AGC_VALID);

  // Expect an agc change.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", -1.0), UintIs("muted", 1),
                                UintIs("agc supported", 1), UintIs("agc enabled", 1)))))))))));

  fuchsia::media::AudioGainInfo gain_info_b{.gain_db = -2.0f, .flags = {}};
  under_test_.SettingDeviceGainInfo(output_device, gain_info_b,
                                    fuchsia::media::AudioGainValidFlags::GAIN_VALID |
                                        fuchsia::media::AudioGainValidFlags::MUTE_VALID |
                                        fuchsia::media::AudioGainValidFlags::AGC_VALID);

  // Expect all changes.
  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                            NameMatches("output_device"),
                            PropertyList(UnorderedElementsAre(
                                DoubleIs("gain db", -2.0), UintIs("muted", 0),
                                UintIs("agc supported", 0), UintIs("agc enabled", 0)))))))))));
}

// Tests methods that add and remove client ports.
TEST_F(ReporterTest, AddRemoveClientPorts) {
  test::NullAudioRenderer renderer_a;
  test::NullAudioRenderer renderer_b;
  test::NullAudioCapturer capturer_a;
  test::NullAudioCapturer capturer_b;

  under_test_.AddingRenderer(renderer_a);
  under_test_.AddingRenderer(renderer_b);
  under_test_.AddingCapturer(capturer_a);
  under_test_.AddingCapturer(capturer_b);

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

  under_test_.RemovingRenderer(renderer_a);
  under_test_.RemovingCapturer(capturer_b);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("2"))))),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("1"))))),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));

  under_test_.AddingRenderer(renderer_a);
  under_test_.AddingCapturer(capturer_b);

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

  under_test_.RemovingRenderer(renderer_a);
  under_test_.RemovingRenderer(renderer_b);
  under_test_.RemovingCapturer(capturer_a);
  under_test_.RemovingCapturer(capturer_b);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("renderers")), ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")), ChildrenMatch(IsEmpty())),
          NodeMatches(NameMatches("output devices")), NodeMatches(NameMatches("input devices")))));
}

// Tests methods that change renderer metrics.
TEST_F(ReporterTest, RendererMetrics) {
  test::NullAudioRenderer renderer;

  under_test_.AddingRenderer(renderer);

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
              NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(UnorderedElementsAre(
                      UintIs("sample format", 0), UintIs("channels", 0),
                      UintIs("frames per second", 0), DoubleIs("gain db", 0.0), UintIs("muted", 0),
                      UintIs("calls to SetGainWithRamp", 0), UintIs("min lead time (ns)", 0),
                      DoubleIs("pts continuity threshold (s)", 0.0),
                      DoubleIs("final stream gain (post-volume) dbfs", 0))))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  under_test_.SettingRendererStreamType(renderer, stream_type);
  under_test_.AddingRendererPayloadBuffer(renderer, 0, 4096);
  under_test_.AddingRendererPayloadBuffer(renderer, 10, 8192);
  under_test_.SendingRendererPacket(renderer, fuchsia::media::StreamPacket{
                                                  .payload_buffer_id = 10,
                                              });
  under_test_.SettingRendererGain(renderer, -1.0);
  under_test_.SettingRendererGainWithRamp(renderer, -1.0, zx::sec(1),
                                          fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingRendererGainWithRamp(renderer, -1.0, zx::sec(1),
                                          fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingRendererMute(renderer, true);
  under_test_.SettingRendererMinLeadTime(renderer, zx::nsec(1000000));
  under_test_.SettingRendererPtsContinuityThreshold(renderer, 5.0);
  under_test_.SettingRendererFinalGain(renderer, -6.0);

  under_test_.RendererStartSession(renderer, zx::time(0));
  under_test_.RendererUnderflow(renderer, zx::time(10), zx::time(15));
  under_test_.RendererStopSession(renderer, zx::time(100));

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
                      DoubleIs("gain db", -1.0), UintIs("muted", 1),
                      UintIs("calls to SetGainWithRamp", 2), UintIs("min lead time (ns)", 1000000),
                      DoubleIs("pts continuity threshold (s)", 5.0),
                      DoubleIs("final stream gain (post-volume) dbfs", -6.0))))))))))));
}

// Tests methods that change capturer metrics.
TEST_F(ReporterTest, CapturerMetrics) {
  test::NullAudioCapturer capturer;

  under_test_.AddingCapturer(capturer);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("payload buffers")), ChildrenMatch(IsEmpty())))),
              NodeMatches(AllOf(NameMatches("1"),
                                PropertyList(UnorderedElementsAre(
                                    UintIs("sample format", 0), UintIs("channels", 0),
                                    UintIs("frames per second", 0), DoubleIs("gain db", 0.0),
                                    UintIs("muted", 0), UintIs("min fence time (ns)", 0),
                                    UintIs("calls to SetGainWithRamp", 0))))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  under_test_.SettingCapturerStreamType(capturer, stream_type);
  under_test_.AddingCapturerPayloadBuffer(capturer, 0, 4096);
  under_test_.AddingCapturerPayloadBuffer(capturer, 10, 8192);
  under_test_.SendingCapturerPacket(capturer, fuchsia::media::StreamPacket{
                                                  .payload_buffer_id = 10,
                                              });
  under_test_.SettingCapturerGain(capturer, -1.0);
  under_test_.SettingCapturerGainWithRamp(capturer, -1.0, zx::sec(1),
                                          fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingCapturerGainWithRamp(capturer, -1.0, zx::sec(1),
                                          fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingCapturerMute(capturer, true);
  under_test_.SettingCapturerMinFenceTime(capturer, zx::nsec(2'000'000));

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(AllOf(
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("payload buffers")),
                  ChildrenMatch(UnorderedElementsAre(
                      NodeMatches(AllOf(NameMatches("0"),
                                        PropertyList(UnorderedElementsAre(
                                            UintIs("size", 4096), UintIs("packets", 0))))),
                      NodeMatches(AllOf(NameMatches("10"),
                                        PropertyList(UnorderedElementsAre(
                                            UintIs("size", 8192), UintIs("packets", 1)))))))))),
              NodeMatches(AllOf(
                  NameMatches("1"),
                  PropertyList(UnorderedElementsAre(
                      UintIs("sample format", static_cast<uint64_t>(stream_type.sample_format)),
                      UintIs("channels", stream_type.channels),
                      UintIs("frames per second", stream_type.frames_per_second),
                      DoubleIs("gain db", -1.0), UintIs("muted", 1),
                      UintIs("min fence time (ns)", 2'000'000),
                      UintIs("calls to SetGainWithRamp", 2))))))))))));
}

}  // namespace
}  // namespace media::audio
