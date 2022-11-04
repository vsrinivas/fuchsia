// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_input.h"

#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/loudness_transform.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

constexpr int64_t kRingBufferSizePages = 8;

class AudioInputTestDriver : public testing::ThreadingModelFixture,
                             public ::testing::WithParamInterface<int32_t> {
 protected:
  AudioInputTestDriver()
      : ThreadingModelFixture(
            ProcessConfig::Builder()
                .AddDeviceProfile(
                    {std::nullopt, DeviceConfig::InputDeviceProfile(
                                       GetParam(), /*driver_gain_db=*/0, /*software_gain_db=*/0)})
                .SetDefaultVolumeCurve(
                    VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                .Build()) {}

  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    remote_driver_ = std::make_unique<testing::FakeAudioDriver>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(remote_driver_, nullptr);

    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
    stream_config.set_channel(std::move(c2));
    input_ =
        AudioInput::Create("", context().process_config().device_config(), std::move(stream_config),
                           &threading_model(), &context().device_manager(),
                           &context().link_matrix(), context().clock_factory());
    ASSERT_NE(input_, nullptr);

    ring_buffer_mapper_ =
        remote_driver_->CreateRingBuffer(kRingBufferSizePages * zx_system_get_page_size());
    ASSERT_NE(ring_buffer_mapper_.start(), nullptr);
  }

  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
  std::shared_ptr<AudioInput> input_;
  fzl::VmoMapper ring_buffer_mapper_;
};

TEST_P(AudioInputTestDriver, RequestHardwareRateInConfigIfSupported) {
  // Publish a format that has a matching sample rate, and also formats with double and half the
  // requested rate.
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  fuchsia::hardware::audio::ChannelSet channel_set = {};
  constexpr size_t kSupportedNumberOfChannels = 1;
  std::vector<fuchsia::hardware::audio::ChannelAttributes> attributes(kSupportedNumberOfChannels);
  channel_set.set_attributes(std::move(attributes));
  formats.mutable_channel_sets()->push_back(std::move(channel_set));
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.mutable_bytes_per_sample()->push_back(2);
  formats.mutable_valid_bits_per_sample()->push_back(16);
  formats.mutable_frame_rates()->push_back(GetParam());
  formats.mutable_frame_rates()->push_back(2 * GetParam());
  formats.mutable_frame_rates()->push_back(GetParam() / 2);
  remote_driver_->set_formats(std::move(formats));

  remote_driver_->Start();
  threading_model().FidlDomain().ScheduleTask(input_->Startup());
  RunLoopUntilIdle();

  auto format = input_->driver()->GetFormat();
  ASSERT_TRUE(format);
  ASSERT_EQ(format->frames_per_second(), GetParam());
}

TEST_P(AudioInputTestDriver, FallBackToAlternativeRateIfPreferredRateIsNotSupported) {
  ASSERT_NE(GetParam(), 0);  // Invalid frame rate passed as test parameter.
  const int32_t kSupportedRate = GetParam() * 2;
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  fuchsia::hardware::audio::ChannelSet channel_set = {};
  constexpr size_t kSupportedNumberOfChannels = 1;
  std::vector<fuchsia::hardware::audio::ChannelAttributes> attributes(kSupportedNumberOfChannels);
  channel_set.set_attributes(std::move(attributes));
  formats.mutable_channel_sets()->push_back(std::move(channel_set));
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.mutable_bytes_per_sample()->push_back(2);
  formats.mutable_valid_bits_per_sample()->push_back(16);
  formats.mutable_frame_rates()->push_back(kSupportedRate);
  remote_driver_->set_formats(std::move(formats));

  remote_driver_->Start();
  threading_model().FidlDomain().ScheduleTask(input_->Startup());
  RunLoopUntilIdle();

  auto format = input_->driver()->GetFormat();
  ASSERT_TRUE(format);
  ASSERT_EQ(format->frames_per_second(), kSupportedRate);
}

INSTANTIATE_TEST_SUITE_P(AudioInputTestDriverInstance, AudioInputTestDriver,
                         ::testing::Values(24000, 48000, 96000));

}  // namespace
}  // namespace media::audio
