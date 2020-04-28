// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/ultrasound/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <zircon/device/audio.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

constexpr uint32_t kUltrasoundSampleRate = 96000;
constexpr uint32_t kUltrasoundChannels = 2;

// This matches the configuration in ultrasound_audio_core_config.json
constexpr std::array<uint8_t, 16> kUltrasoundOutputDeviceId = {{
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
}};
constexpr std::array<uint8_t, 16> kUltrasoundInputDeviceId = {{
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
    0xee,
}};

class UltrasoundTest : public HermeticAudioCoreTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioCoreTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_config_data_path = "/pkg/data/ultrasound",
    });
  }

  void SetUp() override {
    HermeticAudioCoreTest::SetUp();
    environment()->ConnectToService(ultrasound_factory_.NewRequest());
    environment()->ConnectToService(virtualaudio_control_.NewRequest());
    virtualaudio_control_->Enable();
  }
  void TearDown() override {
    // Ensure all devices are now removed.
    fuchsia::media::AudioDeviceEnumeratorSyncPtr enumerator;
    environment()->ConnectToService(enumerator.NewRequest());
    RunLoopUntil([&enumerator] {
      std::vector<fuchsia::media::AudioDeviceInfo> devices;
      zx_status_t status = enumerator->GetDevices(&devices);
      FX_CHECK(status == ZX_OK);
      return devices.empty();
    });

    virtualaudio_control_->Disable();
    HermeticAudioCoreTest::TearDown();
  }

  template <typename Interface>
  struct StreamHolder {
    fidl::InterfacePtr<Interface> stream;
    zx::clock reference_clock;
    fuchsia::media::AudioStreamType stream_type;
  };
  using CapturerHolder = StreamHolder<fuchsia::media::AudioCapturer>;
  using RendererHolder = StreamHolder<fuchsia::media::AudioRenderer>;

  RendererHolder CreateUltrasoundRenderer() {
    RendererHolder holder;

    bool created = false;
    ultrasound_factory_->CreateRenderer(holder.stream.NewRequest(),
                                        [&holder, &created](auto ref_clock, auto stream_type) {
                                          created = true;
                                          holder.reference_clock = std::move(ref_clock);
                                          holder.stream_type = std::move(stream_type);
                                        });
    RunLoopUntil([&created] { return created; });
    holder.stream.set_error_handler(ErrorHandler());
    return holder;
  }

  CapturerHolder CreateUltrasoundCapturer() {
    CapturerHolder holder;

    bool created = false;
    ultrasound_factory_->CreateCapturer(holder.stream.NewRequest(),
                                        [&holder, &created](auto ref_clock, auto stream_type) {
                                          created = true;
                                          holder.reference_clock = std::move(ref_clock);
                                          holder.stream_type = std::move(stream_type);
                                        });
    RunLoopUntil([&created] { return created; });
    holder.stream.set_error_handler(ErrorHandler());
    return holder;
  }

  fuchsia::virtualaudio::OutputPtr AddVirtualOutput(
      const std::array<uint8_t, 16>& output_unique_id) {
    fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum;
    environment()->ConnectToService(audio_dev_enum.NewRequest());

    std::optional<std::vector<fuchsia::media::AudioDeviceInfo>> devices;
    audio_dev_enum->GetDevices(
        [&devices](auto received_devices) { devices = {std::move(received_devices)}; });
    RunLoopUntil([&devices] { return !!devices; });

    bool device_added = false;
    audio_dev_enum.events().OnDeviceAdded = [&device_added](auto device) { device_added = true; };

    fuchsia::virtualaudio::OutputPtr output;
    environment()->ConnectToService(output.NewRequest());
    output.set_error_handler(ErrorHandler());

    const uint32_t kRingFrames = 96000;
    output->SetUniqueId(output_unique_id);
    output->SetRingBufferRestrictions(kRingFrames, kRingFrames, kRingFrames);
    output->SetNotificationFrequency(100);
    output->ClearFormatRanges();
    output->AddFormatRange(AUDIO_SAMPLE_FORMAT_16BIT, kUltrasoundSampleRate, kUltrasoundSampleRate,
                           kUltrasoundChannels, kUltrasoundChannels, ASF_RANGE_FLAG_FPS_CONTINUOUS);
    output->Add();

    RunLoopUntil([this, &device_added] { return device_added || error_occurred_; });
    if (error_occurred_) {
      return nullptr;
    }
    return output;
  }

  fuchsia::virtualaudio::InputPtr AddVirtualInput(const std::array<uint8_t, 16>& input_unique_id) {
    fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum;
    environment()->ConnectToService(audio_dev_enum.NewRequest());

    std::optional<std::vector<fuchsia::media::AudioDeviceInfo>> devices;
    audio_dev_enum->GetDevices(
        [&devices](auto received_devices) { devices = {std::move(received_devices)}; });
    RunLoopUntil([&devices] { return !!devices; });

    bool device_added = false;
    audio_dev_enum.events().OnDeviceAdded = [&device_added](auto device) { device_added = true; };

    fuchsia::virtualaudio::InputPtr input;
    environment()->ConnectToService(input.NewRequest());
    input.set_error_handler(ErrorHandler());

    const uint32_t kRingFrames = 96000;
    input->SetUniqueId(input_unique_id);
    input->SetRingBufferRestrictions(kRingFrames, kRingFrames, kRingFrames);
    input->SetNotificationFrequency(100);
    input->AddFormatRange(AUDIO_SAMPLE_FORMAT_16BIT, kUltrasoundSampleRate, kUltrasoundSampleRate,
                          2, 2, ASF_RANGE_FLAG_FPS_CONTINUOUS);
    input->Add();

    RunLoopUntil([this, &device_added] { return device_added || error_occurred_; });
    if (error_occurred_) {
      return nullptr;
    }
    return input;
  }

  fuchsia::ultrasound::FactoryPtr ultrasound_factory_;
  fuchsia::virtualaudio::ControlSyncPtr virtualaudio_control_;
  zx::clock reference_clock_;

  bool bound_renderer_expected_ = true;
};

TEST_F(UltrasoundTest, CreateRenderer) {
  auto output = AddVirtualOutput(kUltrasoundOutputDeviceId);
  auto holder = CreateUltrasoundRenderer();

  EXPECT_EQ(holder.stream_type.frames_per_second, kUltrasoundSampleRate);
  EXPECT_EQ(holder.stream_type.sample_format, fuchsia::media::AudioSampleFormat::FLOAT);
  EXPECT_EQ(holder.stream_type.channels, kUltrasoundChannels);

  testing::VerifyAppropriateRights(holder.reference_clock);
  testing::VerifyClockAdvances(holder.reference_clock);
  testing::VerifyClockCannotBeRateAdjusted(holder.reference_clock);
  testing::VerifyClockIsSystemMonotonic(holder.reference_clock);
}

TEST_F(UltrasoundTest, RendererDoesNotSupportSetPcmStreamType) {
  auto output = AddVirtualOutput(kUltrasoundOutputDeviceId);
  auto holder = CreateUltrasoundRenderer();

  std::optional<zx_status_t> renderer_error;
  holder.stream.set_error_handler([&renderer_error](auto status) { renderer_error = {status}; });

  // Call SetPcmStreamType. We use the current stream type here to ensure we're definitely
  // requesting a supported stream type.
  holder.stream->SetPcmStreamType(holder.stream_type);

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&renderer_error] { return renderer_error.has_value(); });
  ASSERT_TRUE(renderer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *renderer_error);
}

TEST_F(UltrasoundTest, RendererDoesNotSupportSetUsage) {
  auto output = AddVirtualOutput(kUltrasoundOutputDeviceId);
  auto holder = CreateUltrasoundRenderer();

  std::optional<zx_status_t> renderer_error;
  holder.stream.set_error_handler([&renderer_error](auto status) { renderer_error = {status}; });

  holder.stream->SetUsage(fuchsia::media::AudioRenderUsage::MEDIA);

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&renderer_error] { return renderer_error.has_value(); });
  ASSERT_TRUE(renderer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *renderer_error);
}

TEST_F(UltrasoundTest, RendererDoesNotSupportBindGainControl) {
  auto output = AddVirtualOutput(kUltrasoundOutputDeviceId);
  auto holder = CreateUltrasoundRenderer();

  std::optional<zx_status_t> renderer_error;
  holder.stream.set_error_handler([&renderer_error](auto status) { renderer_error = {status}; });

  fuchsia::media::audio::GainControlPtr gain_control;
  holder.stream->BindGainControl(gain_control.NewRequest());

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&renderer_error] { return renderer_error.has_value(); });
  ASSERT_TRUE(renderer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *renderer_error);
}

TEST_F(UltrasoundTest, RendererDoesNotSupportSetReferenceClock) {
  auto output = AddVirtualOutput(kUltrasoundOutputDeviceId);
  auto holder = CreateUltrasoundRenderer();

  std::optional<zx_status_t> renderer_error;
  holder.stream.set_error_handler([&renderer_error](auto status) { renderer_error = {status}; });

  // Call SetPcmStreamType. We use the current stream type here to ensure we're definitely
  // requesting a supported stream type.
  zx::clock clock_to_set;
  ASSERT_EQ(ZX_OK, clock::DuplicateClock(holder.reference_clock, &clock_to_set));
  holder.stream->SetReferenceClock(std::move(clock_to_set));

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&renderer_error] { return renderer_error.has_value(); });
  ASSERT_TRUE(renderer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *renderer_error);
}

TEST_F(UltrasoundTest, CreateCapturer) {
  auto input = AddVirtualInput(kUltrasoundInputDeviceId);
  auto holder = CreateUltrasoundCapturer();

  EXPECT_EQ(holder.stream_type.frames_per_second, kUltrasoundSampleRate);
  EXPECT_EQ(holder.stream_type.sample_format, fuchsia::media::AudioSampleFormat::FLOAT);
  EXPECT_EQ(holder.stream_type.channels, kUltrasoundChannels);

  testing::VerifyAppropriateRights(holder.reference_clock);
  testing::VerifyClockAdvances(holder.reference_clock);
  testing::VerifyClockCannotBeRateAdjusted(holder.reference_clock);
  testing::VerifyClockIsSystemMonotonic(holder.reference_clock);
}

TEST_F(UltrasoundTest, CapturerDoesNotSupportSetPcmStreamType) {
  auto output = AddVirtualInput(kUltrasoundInputDeviceId);
  auto holder = CreateUltrasoundCapturer();

  std::optional<zx_status_t> capturer_error;
  holder.stream.set_error_handler([&capturer_error](auto status) { capturer_error = {status}; });

  // Call SetPcmStreamType. We use the current stream type here to ensure we're definitely
  // requesting a supported stream type.
  holder.stream->SetPcmStreamType(holder.stream_type);

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&capturer_error] { return capturer_error.has_value(); });
  ASSERT_TRUE(capturer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *capturer_error);
}

TEST_F(UltrasoundTest, CapturerDoesNotSupportSetUsage) {
  auto output = AddVirtualInput(kUltrasoundInputDeviceId);
  auto holder = CreateUltrasoundCapturer();

  std::optional<zx_status_t> capturer_error;
  holder.stream.set_error_handler([&capturer_error](auto status) { capturer_error = {status}; });

  holder.stream->SetUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&capturer_error] { return capturer_error.has_value(); });
  ASSERT_TRUE(capturer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *capturer_error);
}

TEST_F(UltrasoundTest, CapturerDoesNotSupportBindGainControl) {
  auto output = AddVirtualInput(kUltrasoundInputDeviceId);
  auto holder = CreateUltrasoundCapturer();

  std::optional<zx_status_t> capturer_error;
  holder.stream.set_error_handler([&capturer_error](auto status) { capturer_error = {status}; });

  fuchsia::media::audio::GainControlPtr gain_control;
  holder.stream->BindGainControl(gain_control.NewRequest());

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&capturer_error] { return capturer_error.has_value(); });
  ASSERT_TRUE(capturer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *capturer_error);
}

TEST_F(UltrasoundTest, CapturerDoesNotSupportSetReferenceClock) {
  auto output = AddVirtualInput(kUltrasoundInputDeviceId);
  auto holder = CreateUltrasoundCapturer();

  std::optional<zx_status_t> capturer_error;
  holder.stream.set_error_handler([&capturer_error](auto status) { capturer_error = {status}; });

  // Call SetPcmStreamType. We use the current stream type here to ensure we're definitely
  // requesting a supported stream type.
  zx::clock clock_to_set;
  ASSERT_EQ(ZX_OK, clock::DuplicateClock(holder.reference_clock, &clock_to_set));
  holder.stream->SetReferenceClock(std::move(clock_to_set));

  // Now expect we get disconnected with ZX_ERR_NOT_SUPPORTED.
  RunLoopUntil([&capturer_error] { return capturer_error.has_value(); });
  ASSERT_TRUE(capturer_error);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *capturer_error);
}

}  // namespace media::audio::test
