// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/inspect/testing/inspect.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/media/audio/audio_core/audio_device.h"

namespace media::audio::test {

using inspect::testing::ChildrenMatch;
using inspect::testing::DoubleMetricIs;
using inspect::testing::MetricList;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UIntMetricIs;
using testing::AllOf;
using testing::IsEmpty;

class ReporterTest : public gtest::TestLoopFixture {
 public:
  ReporterTest() {
    std::unique_ptr<sys::ComponentContext> component_context =
        sys::ComponentContext::Create();
    under_test_.Init(component_context.get());
  }

  inspect::ObjectHierarchy GetHierarchy() {
    zx::vmo duplicate;
    if (under_test_.tree().GetVmo().duplicate(ZX_RIGHT_SAME_RIGHTS,
                                              &duplicate) != ZX_OK) {
      return inspect::ObjectHierarchy();
    }

    auto ret = inspect::ReadFromVmo(std::move(duplicate));
    EXPECT_TRUE(ret.is_ok());
    if (ret.is_ok()) {
      return ret.take_value();
    }

    return inspect::ObjectHierarchy();
  }

  Reporter under_test_;
};

class TestInput : public AudioDevice {
 public:
  TestInput()
      : AudioDevice(Type::Input, reinterpret_cast<AudioDeviceManager*>(1)) {}
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       uint32_t set_flags) override {}
  void OnWakeup() override {}
};

class TestOutput : public AudioDevice {
 public:
  TestOutput()
      : AudioDevice(Type::Output, reinterpret_cast<AudioDeviceManager*>(1)) {}
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       uint32_t set_flags) override {}
  void OnWakeup() override {}
};

// Tests reporter initial state.
TEST_F(ReporterTest, InitialState) {
  auto hierarchy = GetHierarchy();

  // Expect metrics with default values in the root node.
  EXPECT_THAT(
      hierarchy,
      NodeMatches(AllOf(
          NameMatches("root"),
          MetricList(UnorderedElementsAre(
              UIntMetricIs("count of failures to open device", 0),
              UIntMetricIs(
                  "count of failures to obtain device fdio service channel", 0),
              UIntMetricIs("count of failures to obtain device stream channel",
                           0),
              UIntMetricIs("count of failures to start a device", 0))))));

  // Expect empty child nodes for devices and client ports.
  EXPECT_THAT(hierarchy,
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(AllOf(NameMatches("output devices"),
                                          PropertyList(IsEmpty()),
                                          MetricList(IsEmpty()))),
                        ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(AllOf(NameMatches("input devices"),
                                          PropertyList(IsEmpty()),
                                          MetricList(IsEmpty()))),
                        ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(AllOf(NameMatches("renderers"),
                                          PropertyList(IsEmpty()),
                                          MetricList(IsEmpty()))),
                        ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(AllOf(NameMatches("capturers"),
                                          PropertyList(IsEmpty()),
                                          MetricList(IsEmpty()))),
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
  TestInput device;
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);
  under_test_.DeviceStartupFailed(device);

  EXPECT_THAT(
      GetHierarchy(),
      NodeMatches(AllOf(
          NameMatches("root"),
          MetricList(UnorderedElementsAre(
              UIntMetricIs("count of failures to open device", 1u),
              UIntMetricIs(
                  "count of failures to obtain device fdio service channel",
                  2u),
              UIntMetricIs("count of failures to obtain device stream channel",
                           3u),
              UIntMetricIs("count of failures to start a device", 4u))))));
}

// Tests methods that add and remove devices.
TEST_F(ReporterTest, AddRemoveDevices) {
  TestOutput output_device_a;
  TestOutput output_device_b;
  TestInput input_device_a;
  TestInput input_device_b;

  under_test_.AddingDevice("output_device_a", output_device_a);
  under_test_.AddingDevice("output_device_b", output_device_b);
  under_test_.AddingDevice("input_device_a", input_device_a);
  under_test_.AddingDevice("input_device_b", input_device_b);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("output_device_a")),
                            NodeMatches(NameMatches("output_device_b"))))),
                  AllOf(NodeMatches(NameMatches("input devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("input_device_a")),
                            NodeMatches(NameMatches("input_device_b"))))),
                  NodeMatches(NameMatches("renderers")),
                  NodeMatches(NameMatches("capturers")))));

  under_test_.RemovingDevice(output_device_a);
  under_test_.RemovingDevice(input_device_b);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("output_device_b"))))),
                  AllOf(NodeMatches(NameMatches("input devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("input_device_a"))))),
                  NodeMatches(NameMatches("renderers")),
                  NodeMatches(NameMatches("capturers")))));

  under_test_.AddingDevice("output_device_a", output_device_a);
  under_test_.AddingDevice("input_device_b", input_device_b);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("output_device_a")),
                            NodeMatches(NameMatches("output_device_b"))))),
                  AllOf(NodeMatches(NameMatches("input devices")),
                        ChildrenMatch(UnorderedElementsAre(
                            NodeMatches(NameMatches("input_device_a")),
                            NodeMatches(NameMatches("input_device_b"))))),
                  NodeMatches(NameMatches("renderers")),
                  NodeMatches(NameMatches("capturers")))));

  under_test_.RemovingDevice(output_device_a);
  under_test_.RemovingDevice(output_device_b);
  under_test_.RemovingDevice(input_device_a);
  under_test_.RemovingDevice(input_device_b);

  EXPECT_THAT(GetHierarchy(),
              ChildrenMatch(UnorderedElementsAre(
                  AllOf(NodeMatches(NameMatches("output devices")),
                        ChildrenMatch(IsEmpty())),
                  AllOf(NodeMatches(NameMatches("input devices")),
                        ChildrenMatch(IsEmpty())),
                  NodeMatches(NameMatches("renderers")),
                  NodeMatches(NameMatches("capturers")))));
}

// Tests the initial state of added devices.
TEST_F(ReporterTest, DeviceInitialState) {
  TestOutput output_device;
  TestInput input_device;

  under_test_.AddingDevice("output_device", output_device);
  under_test_.AddingDevice("input_device", input_device);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(
              NodeMatches(NameMatches("output devices")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("output_device"),
                  MetricList(UnorderedElementsAre(
                      DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                      UIntMetricIs("agc supported", 0),
                      UIntMetricIs("agc enabled", 0)))))))),
          AllOf(
              NodeMatches(NameMatches("input devices")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("input_device"),
                  MetricList(UnorderedElementsAre(
                      DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                      UIntMetricIs("agc supported", 0),
                      UIntMetricIs("agc enabled", 0)))))))),
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(IsEmpty())))));
}

// Tests method SettingDeviceGainInfo.
TEST_F(ReporterTest, SettingDeviceGainInfo) {
  TestOutput output_device;
  TestInput input_device;

  under_test_.AddingDevice("output_device", output_device);

  // Expect initial device metric values.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(
              NodeMatches(NameMatches("output devices")),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                  NameMatches("output_device"),
                  MetricList(UnorderedElementsAre(
                      DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                      UIntMetricIs("agc supported", 0),
                      UIntMetricIs("agc enabled", 0)))))))),
          AllOf(NodeMatches(NameMatches("input devices")),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("renderers")),
                ChildrenMatch(IsEmpty())),
          AllOf(NodeMatches(NameMatches("capturers")),
                ChildrenMatch(IsEmpty())))));

  fuchsia::media::AudioGainInfo gain_info_a{
      .gain_db = -1.0f,
      .flags = fuchsia::media::AudioGainInfoFlag_Mute |
               fuchsia::media::AudioGainInfoFlag_AgcSupported |
               fuchsia::media::AudioGainInfoFlag_AgcEnabled};

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a, 0);

  // Expect initial device metric values.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("output_device"),
              MetricList(UnorderedElementsAre(
                  DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                  UIntMetricIs("agc supported", 0),
                  UIntMetricIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::SetAudioGainFlag_GainValid);

  // Expect a gain change.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("output_device"),
              MetricList(UnorderedElementsAre(
                  DoubleMetricIs("gain db", -1.0), UIntMetricIs("muted", 0),
                  UIntMetricIs("agc supported", 0),
                  UIntMetricIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::SetAudioGainFlag_MuteValid);

  // Expect a mute change.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("output_device"),
              MetricList(UnorderedElementsAre(
                  DoubleMetricIs("gain db", -1.0), UIntMetricIs("muted", 1),
                  UIntMetricIs("agc supported", 0),
                  UIntMetricIs("agc enabled", 0)))))))))));

  under_test_.SettingDeviceGainInfo(output_device, gain_info_a,
                                    fuchsia::media::SetAudioGainFlag_AgcValid);

  // Expect an agc change.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("output_device"),
              MetricList(UnorderedElementsAre(
                  DoubleMetricIs("gain db", -1.0), UIntMetricIs("muted", 1),
                  UIntMetricIs("agc supported", 1),
                  UIntMetricIs("agc enabled", 1)))))))))));

  fuchsia::media::AudioGainInfo gain_info_b{.gain_db = -2.0f, .flags = 0};
  under_test_.SettingDeviceGainInfo(
      output_device, gain_info_b,
      fuchsia::media::SetAudioGainFlag_GainValid |
          fuchsia::media::SetAudioGainFlag_MuteValid |
          fuchsia::media::SetAudioGainFlag_AgcValid);

  // Expect all changes.
  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("output devices")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("output_device"),
              MetricList(UnorderedElementsAre(
                  DoubleMetricIs("gain db", -2.0), UIntMetricIs("muted", 0),
                  UIntMetricIs("agc supported", 0),
                  UIntMetricIs("agc enabled", 0)))))))))));
}

// Tests methods that add and remove client ports.
TEST_F(ReporterTest, AddRemoveClientPorts) {
  AudioRendererImpl& renderer_a = *(reinterpret_cast<AudioRendererImpl*>(1));
  AudioRendererImpl& renderer_b = *(reinterpret_cast<AudioRendererImpl*>(2));
  AudioCapturerImpl& capturer_a = *(reinterpret_cast<AudioCapturerImpl*>(3));
  AudioCapturerImpl& capturer_b = *(reinterpret_cast<AudioCapturerImpl*>(4));

  under_test_.AddingRenderer(renderer_a);
  under_test_.AddingRenderer(renderer_b);
  under_test_.AddingCapturer(capturer_a);
  under_test_.AddingCapturer(capturer_b);

  EXPECT_THAT(GetHierarchy(), ChildrenMatch(UnorderedElementsAre(
                                  AllOf(NodeMatches(NameMatches("renderers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("1")),
                                            NodeMatches(NameMatches("2"))))),
                                  AllOf(NodeMatches(NameMatches("capturers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("1")),
                                            NodeMatches(NameMatches("2"))))),
                                  NodeMatches(NameMatches("output devices")),
                                  NodeMatches(NameMatches("input devices")))));

  under_test_.RemovingRenderer(renderer_a);
  under_test_.RemovingCapturer(capturer_b);

  EXPECT_THAT(GetHierarchy(), ChildrenMatch(UnorderedElementsAre(
                                  AllOf(NodeMatches(NameMatches("renderers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("2"))))),
                                  AllOf(NodeMatches(NameMatches("capturers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("1"))))),
                                  NodeMatches(NameMatches("output devices")),
                                  NodeMatches(NameMatches("input devices")))));

  under_test_.AddingRenderer(renderer_a);
  under_test_.AddingCapturer(capturer_b);

  EXPECT_THAT(GetHierarchy(), ChildrenMatch(UnorderedElementsAre(
                                  AllOf(NodeMatches(NameMatches("renderers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("3")),
                                            NodeMatches(NameMatches("2"))))),
                                  AllOf(NodeMatches(NameMatches("capturers")),
                                        ChildrenMatch(UnorderedElementsAre(
                                            NodeMatches(NameMatches("1")),
                                            NodeMatches(NameMatches("3"))))),
                                  NodeMatches(NameMatches("output devices")),
                                  NodeMatches(NameMatches("input devices")))));

  under_test_.RemovingRenderer(renderer_a);
  under_test_.RemovingRenderer(renderer_b);
  under_test_.RemovingCapturer(capturer_a);
  under_test_.RemovingCapturer(capturer_b);

  EXPECT_THAT(GetHierarchy(), ChildrenMatch(UnorderedElementsAre(
                                  AllOf(NodeMatches(NameMatches("renderers")),
                                        ChildrenMatch(IsEmpty())),
                                  AllOf(NodeMatches(NameMatches("capturers")),
                                        ChildrenMatch(IsEmpty())),
                                  NodeMatches(NameMatches("output devices")),
                                  NodeMatches(NameMatches("input devices")))));
}

// Tests methods that change renderer metrics.
TEST_F(ReporterTest, RendererMetrics) {
  AudioRendererImpl& renderer = *(reinterpret_cast<AudioRendererImpl*>(1));

  under_test_.AddingRenderer(renderer);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("renderers")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("1"),
              MetricList(UnorderedElementsAre(
                  UIntMetricIs("sample format", 0), UIntMetricIs("channels", 0),
                  UIntMetricIs("frames per second", 0),
                  UIntMetricIs("payload buffer size", 0),
                  DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                  UIntMetricIs("calls to SetGainWithRamp", 0),
                  UIntMetricIs("min clock lead time (ns)", 0),
                  DoubleMetricIs("pts continuity threshold (s)", 0.0)))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  under_test_.SettingRendererStreamType(renderer, stream_type);
  under_test_.AddingRendererPayloadBuffer(renderer, 0, 4096);
  under_test_.SettingRendererGain(renderer, -1.0);
  under_test_.SettingRendererGainWithRamp(
      renderer, -1.0, ZX_SEC(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingRendererGainWithRamp(
      renderer, -1.0, ZX_SEC(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingRendererMute(renderer, true);
  under_test_.SettingRendererMinClockLeadTime(renderer, 1000000);
  under_test_.SettingRendererPtsContinuityThreshold(renderer, 5.0);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("renderers")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("1"),
              MetricList(UnorderedElementsAre(
                  UIntMetricIs("sample format", static_cast<uint64_t>(
                                                    stream_type.sample_format)),
                  UIntMetricIs("channels", stream_type.channels),
                  UIntMetricIs("frames per second",
                               stream_type.frames_per_second),
                  UIntMetricIs("payload buffer size", 4096),
                  DoubleMetricIs("gain db", -1.0), UIntMetricIs("muted", 1),
                  UIntMetricIs("calls to SetGainWithRamp", 2),
                  UIntMetricIs("min clock lead time (ns)", 1000000),
                  DoubleMetricIs("pts continuity threshold (s)", 5.0)))))))))));
}

// Tests methods that change capturer metrics.
TEST_F(ReporterTest, CapturerMetrics) {
  AudioCapturerImpl& capturer = *(reinterpret_cast<AudioCapturerImpl*>(1));

  under_test_.AddingCapturer(capturer);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("1"),
              MetricList(UnorderedElementsAre(
                  UIntMetricIs("sample format", 0), UIntMetricIs("channels", 0),
                  UIntMetricIs("frames per second", 0),
                  UIntMetricIs("payload buffer size", 0),
                  DoubleMetricIs("gain db", 0.0), UIntMetricIs("muted", 0),
                  UIntMetricIs("calls to SetGainWithRamp", 0)))))))))));

  fuchsia::media::AudioStreamType stream_type{
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 48000};
  under_test_.SettingCapturerStreamType(capturer, stream_type);
  under_test_.AddingCapturerPayloadBuffer(capturer, 0, 4096);
  under_test_.SettingCapturerGain(capturer, -1.0);
  under_test_.SettingCapturerGainWithRamp(
      capturer, -1.0, ZX_SEC(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingCapturerGainWithRamp(
      capturer, -1.0, ZX_SEC(1), fuchsia::media::audio::RampType::SCALE_LINEAR);
  under_test_.SettingCapturerMute(capturer, true);

  EXPECT_THAT(
      GetHierarchy(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("capturers")),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("1"),
              MetricList(UnorderedElementsAre(
                  UIntMetricIs("sample format", static_cast<uint64_t>(
                                                    stream_type.sample_format)),
                  UIntMetricIs("channels", stream_type.channels),
                  UIntMetricIs("frames per second",
                               stream_type.frames_per_second),
                  UIntMetricIs("payload buffer size", 4096),
                  DoubleMetricIs("gain db", -1.0), UIntMetricIs("muted", 1),
                  UIntMetricIs("calls to SetGainWithRamp", 2)))))))))));
}

}  // namespace media::audio::test
