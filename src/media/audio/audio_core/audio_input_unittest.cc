// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_input.h"

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/loudness_transform.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

constexpr size_t kRingBufferSizeBytes = 8 * PAGE_SIZE;

class AudioInputTest : public testing::ThreadingModelFixture,
                       public ::testing::WithParamInterface<uint32_t> {
 protected:
  AudioInputTest()
      : ThreadingModelFixture(
            ProcessConfig::Builder()
                .AddDeviceProfile({std::nullopt, DeviceConfig::InputDeviceProfile(GetParam())})
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

    input_ = AudioInput::Create(std::move(c2), &threading_model(), &context().device_manager(),
                                &context().link_matrix());
    ASSERT_NE(input_, nullptr);

    ring_buffer_mapper_ = remote_driver_->CreateRingBuffer(kRingBufferSizeBytes);
    ASSERT_NE(ring_buffer_mapper_.start(), nullptr);
  }

  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
  std::shared_ptr<AudioInput> input_;
  fzl::VmoMapper ring_buffer_mapper_;
};

TEST_P(AudioInputTest, RequestHardwareRateInConfigIfSupported) {
  // Publish a format that has a matching sample rate, and also formats with double and half the
  // requested rate.
  remote_driver_->set_formats({{
                                   .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
                                   .min_frames_per_second = GetParam(),
                                   .max_frames_per_second = GetParam(),
                                   .min_channels = 1,
                                   .max_channels = 1,
                                   .flags = ASF_RANGE_FLAG_FPS_CONTINUOUS,
                               },
                               {
                                   .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
                                   .min_frames_per_second = 2 * GetParam(),
                                   .max_frames_per_second = 2 * GetParam(),
                                   .min_channels = 1,
                                   .max_channels = 1,
                                   .flags = ASF_RANGE_FLAG_FPS_CONTINUOUS,
                               },
                               {
                                   .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
                                   .min_frames_per_second = GetParam() / 2,
                                   .max_frames_per_second = GetParam() / 2,
                                   .min_channels = 1,
                                   .max_channels = 1,
                                   .flags = ASF_RANGE_FLAG_FPS_CONTINUOUS,
                               }});

  remote_driver_->Start();
  threading_model().FidlDomain().ScheduleTask(input_->Startup());
  RunLoopUntilIdle();

  auto format = input_->driver()->GetFormat();
  ASSERT_TRUE(format);
  ASSERT_EQ(format->frames_per_second(), GetParam());
}

TEST_P(AudioInputTest, FallBackToAlternativeRateIfPreferredRateIsNotSupported) {
  const uint32_t kSupportedRate = GetParam() * 2;
  remote_driver_->set_formats({{
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = kSupportedRate,
      .max_frames_per_second = kSupportedRate,
      .min_channels = 1,
      .max_channels = 1,
      .flags = ASF_RANGE_FLAG_FPS_CONTINUOUS,
  }});

  remote_driver_->Start();
  threading_model().FidlDomain().ScheduleTask(input_->Startup());
  RunLoopUntilIdle();

  auto format = input_->driver()->GetFormat();
  ASSERT_TRUE(format);
  ASSERT_EQ(format->frames_per_second(), kSupportedRate);
  ASSERT_NE(kSupportedRate, GetParam());
}

INSTANTIATE_TEST_SUITE_P(AudioInputTestInstance, AudioInputTest,
                         ::testing::Values(24000, 48000, 96000));

}  // namespace
}  // namespace media::audio
