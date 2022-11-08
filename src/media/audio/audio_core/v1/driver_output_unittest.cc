// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/driver_output.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <fbl/ref_ptr.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/shared/loudness_transform.h"
#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects_v1.h"
#include "src/media/audio/lib/format/driver_format.h"
#include "src/media/audio/lib/processing/gain.h"

using testing::Each;
using testing::Eq;
using testing::FloatEq;

namespace media::audio {
namespace {

constexpr zx::duration kBeyondSubmittedPackets = zx::sec(1);

int64_t RingBufferSizeBytes() { return 8 * zx_system_get_page_size(); }

class DriverOutputTest : public testing::ThreadingModelFixture {
 protected:
  static constexpr int32_t kRequestedDeviceRate = 48000;
  static constexpr int16_t kRequestedDeviceChannels = 4;
  static PipelineConfig CreatePipelineConfig() {
    PipelineConfig config;
    config.mutable_root().name = "default";
    config.mutable_root().input_streams = {
        RenderUsage::BACKGROUND,   RenderUsage::MEDIA,         RenderUsage::INTERRUPTION,
        RenderUsage::SYSTEM_AGENT, RenderUsage::COMMUNICATION,
    };
    config.mutable_root().output_rate = kRequestedDeviceRate;
    config.mutable_root().output_channels = kRequestedDeviceChannels / 2;
    config.mutable_root().loopback = true;
    config.mutable_root().effects_v1 = {{
        .lib_name = testing::kTestEffectsModuleName,
        .effect_name = "rechannel",
        .instance_name = "1:2 upchannel",
        .effect_config = "",
        .output_channels = kRequestedDeviceChannels,
    }};
    return config;
  }

  DriverOutputTest()
      : ThreadingModelFixture(
            ProcessConfig::Builder()
                .AddDeviceProfile(
                    {std::nullopt,
                     DeviceConfig::OutputDeviceProfile(
                         /* eligible_for_loopback */ true,
                         StreamUsageSetFromRenderUsages(kFidlRenderUsages),
                         VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                         /* independent_volume_control */ false, CreatePipelineConfig(),
                         /* driver_gain_db */ 0.0, /* software_gain_db */ 0.0)})
                .SetDefaultVolumeCurve(
                    VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                .Build()),
        expected_mix_interval_(context().process_config().mix_profile_config().period) {}

  void AddChannelSet(fuchsia::hardware::audio::PcmSupportedFormats& formats,
                     size_t number_of_channels) {
    fuchsia::hardware::audio::ChannelSet channel_set = {};
    std::vector<fuchsia::hardware::audio::ChannelAttributes> attributes(number_of_channels);
    channel_set.set_attributes(std::move(attributes));
    formats.mutable_channel_sets()->push_back(std::move(channel_set));
  }

  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    driver_ = std::make_unique<testing::FakeAudioDriver>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(driver_, nullptr);
    driver_->Start();

    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
    stream_config.set_channel(std::move(c2));
    output_ = std::make_shared<DriverOutput>("", context().process_config().device_config(),
                                             context().process_config().mix_profile_config(),
                                             &threading_model(), &context().device_manager(),
                                             std::move(stream_config), &context().link_matrix(),
                                             context().clock_factory(),
                                             nullptr);  // not using V2 effects
    ASSERT_NE(output_, nullptr);

    ring_buffer_mapper_ = driver_->CreateRingBuffer(RingBufferSizeBytes());
    ASSERT_NE(ring_buffer_mapper_.start(), nullptr);

    // Add a rechannel effect.
    test_effects_.AddEffect("rechannel")
        .WithChannelization(FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY, FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY)
        .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  }

  // Use len = -1 for the end of the buffer.
  template <typename T>
  cpp20::span<T> RingBufferSlice(size_t first, ssize_t maybe_len) const {
    assert(RingBufferSizeBytes() % sizeof(T) == 0);
    T* array = static_cast<T*>(ring_buffer_mapper_.start());
    size_t len = maybe_len >= 0 ? maybe_len : (RingBufferSizeBytes() / sizeof(T)) - first;
    FX_CHECK((first + len) * sizeof(T) <= static_cast<size_t>(RingBufferSizeBytes()));
    return {&array[first], len};
  }

  template <typename T>
  cpp20::span<T> RingBuffer() const {
    return RingBufferSlice<T>(0, RingBufferSizeBytes() / sizeof(T));
  }

  // Updates the driver to advertise the given format. This will be the only audio format that the
  // driver exposes.
  void ConfigureDriverForSampleFormat(fuchsia::hardware::audio::PcmFormat sample_format) {
    fuchsia::hardware::audio::PcmSupportedFormats formats = {};
    AddChannelSet(formats, sample_format.number_of_channels);
    formats.mutable_sample_formats()->push_back(sample_format.sample_format);
    formats.mutable_bytes_per_sample()->push_back(sample_format.bytes_per_sample);
    formats.mutable_valid_bits_per_sample()->push_back(sample_format.valid_bits_per_sample);
    formats.mutable_frame_rates()->push_back(sample_format.frame_rate);
    ConfigureDriverForSampleFormats(std::move(formats));
  }
  void ConfigureDriverForSampleFormats(fuchsia::hardware::audio::PcmSupportedFormats formats) {
    driver_->set_formats(std::move(formats));
  }

  testing::TestEffectsV1Module test_effects_ = testing::TestEffectsV1Module::Open();
  zx::duration expected_mix_interval_;
  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(media_audio::kMinGainDb);
  std::unique_ptr<testing::FakeAudioDriver> driver_;
  std::shared_ptr<DriverOutput> output_;
  fzl::VmoMapper ring_buffer_mapper_;
  zx::vmo ring_buffer_;
};

// Simple sanity test that the DriverOutput properly initializes the driver.
TEST_F(DriverOutputTest, DriverOutputStartsDriver) {
  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  auto rb_bytes = RingBuffer<uint8_t>();
  memset(rb_bytes.data(), 0xff, rb_bytes.size());

  // Setup our driver to advertise support for only 24-bit/2-channel/48khz audio.
  fuchsia::hardware::audio::PcmFormat supportedSampleFormat = {};
  supportedSampleFormat.sample_format = fuchsia::hardware::audio::SampleFormat::PCM_SIGNED;
  supportedSampleFormat.bytes_per_sample = 4;
  supportedSampleFormat.valid_bits_per_sample = 24;
  supportedSampleFormat.number_of_channels = 2;
  supportedSampleFormat.frame_rate = 48000;
  ConfigureDriverForSampleFormat(supportedSampleFormat);

  // Startup the DriverOutput. We expect it's completed some basic initialization of the driver.
  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // Verify the DriverOutput has requested a ring buffer with the correct format type. Since we only
  // published support for a single format above, there's only one possible solution here.
  auto selected_format = driver_->selected_format();
  EXPECT_TRUE(selected_format);
  auto& selected = selected_format.value();
  EXPECT_EQ(selected.sample_format, supportedSampleFormat.sample_format);
  EXPECT_EQ(selected.bytes_per_sample, supportedSampleFormat.bytes_per_sample);
  EXPECT_EQ(selected.valid_bits_per_sample, supportedSampleFormat.valid_bits_per_sample);
  EXPECT_EQ(selected.number_of_channels, supportedSampleFormat.number_of_channels);
  EXPECT_EQ(selected.frame_rate, supportedSampleFormat.frame_rate);

  // We expect the driver has filled the buffer with silence. For 16-bit/2-channel audio, we can
  // represent each frame as a single uint32_t.
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<float>(), Each(FloatEq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, RendererOutput) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmFormat supportedSampleFormat = {};
  supportedSampleFormat.sample_format = fuchsia::hardware::audio::SampleFormat::PCM_SIGNED;
  supportedSampleFormat.bytes_per_sample = 2;
  supportedSampleFormat.valid_bits_per_sample = 16;
  supportedSampleFormat.number_of_channels = 2;
  supportedSampleFormat.frame_rate = 48000;
  ConfigureDriverForSampleFormat(supportedSampleFormat);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(
      dispatcher(), &context().link_matrix(), context().clock_factory());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  renderer->EnqueueAudioPacket(-0.5, zx::msec(5));
  renderer->EnqueueAudioPacket(-0.5, zx::msec(5));
  // Only the first two packets will be mixed; we'll stop before mixing the third.
  bool packet3_released = false;
  renderer->EnqueueAudioPacket(0.8765, zx::msec(5),
                               [&packet3_released] { packet3_released = true; });

  // Run the loop for just before we expect the mix to occur to validate we're mixing on the correct
  // interval.
  RunLoopFor(expected_mix_interval_ - zx::nsec(1));
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now run for that last instant and expect a mix has occurred.
  RunLoopFor(zx::nsec(1));
  // Expect 3 sections of the ring:
  //   [0, first_non_silent_frame) - Silence (corresponds to the mix lead time).
  //   [first_non_silent_frame, first_silent_frame) - Non silent samples (corresponds to -0.5
  //       samples provided by renderer: 0xC000 in int16; 0xC000C000 for entire frame as uint32).
  //   [first_silent_frame, ring_buffer.size()) - Silence again (we did not provide any data to
  //       mix at this point in the ring buffer).
  const uint32_t kNonSilentFrame = 0xC000C000;
  const uint32_t kMixWindowFrames = 480;
  size_t first_non_silent_frame =
      (supportedSampleFormat.frame_rate * output_->presentation_delay().to_nsecs()) / 1'000'000'000;
  size_t first_silent_frame = first_non_silent_frame + kMixWindowFrames;

  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_non_silent_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_non_silent_frame, kMixWindowFrames),
              Each(Eq(kNonSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));
  EXPECT_FALSE(packet3_released);

  // Play out any remaining packets, so the slab_allocator won't assert on debug builds.
  RunLoopFor(kBeyondSubmittedPackets);
  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, MixAtExpectedInterval) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmFormat supportedSampleFormat = {};
  supportedSampleFormat.sample_format = fuchsia::hardware::audio::SampleFormat::PCM_SIGNED;
  supportedSampleFormat.bytes_per_sample = 2;
  supportedSampleFormat.valid_bits_per_sample = 16;
  supportedSampleFormat.number_of_channels = 2;
  supportedSampleFormat.frame_rate = 48000;

  // 5ms at our chosen sample rate.
  constexpr uint32_t kFifoDepth = 240;
  constexpr zx::duration kExternalDelay = zx::usec(47376);
  driver_->set_fifo_depth(kFifoDepth);
  driver_->set_external_delay(kExternalDelay);
  ConfigureDriverForSampleFormat(supportedSampleFormat);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(
      dispatcher(), &context().link_matrix(), context().clock_factory());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  renderer->EnqueueAudioPacket(0.75, expected_mix_interval_);
  renderer->EnqueueAudioPacket(-0.75, expected_mix_interval_);

  // We'll have 4 sections in our ring buffer:
  //  *  Silence during the initial lead time.
  //  *  10ms of frames that contain 0.75 float data.
  //  *  10ms of frames that contain -0.75 float data.
  //  *  Silence during the rest of the ring.
  const uint32_t kSilentFrame = 0;
  const uint32_t kPostiveFrame = 0x60006000;
  const uint32_t kNegativeFrame = 0xA000A000;
  const uint32_t kMixWindowFrames = 480;

  // Renderer clients need to provide packets early, by the amount presentation_delay.
  // Audio data will be mixed into the ring buffer, offset by exactly that amount EXCEPT the
  // external_delay component, which is a post-interconnect delay.
  size_t first_positive_frame = (supportedSampleFormat.frame_rate *
                                 (output_->presentation_delay() - kExternalDelay).to_nsecs()) /
                                1'000'000'000;
  size_t first_negative_frame = first_positive_frame + kMixWindowFrames;
  size_t first_silent_frame = first_negative_frame + kMixWindowFrames;

  // Run until just before the expected first mix. Expect the ring buffer to be empty.
  RunLoopFor(expected_mix_interval_ - zx::nsec(1));
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now expect the first mix, which adds the positive samples
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_frame, kMixWindowFrames),
              Each(Eq(kPostiveFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_frame, -1), Each(Eq(kSilentFrame)));

  // Run until just before the next mix interval. Expect the ring to be unchanged.
  RunLoopFor(expected_mix_interval_ - zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_frame, kMixWindowFrames),
              Each(Eq(kPostiveFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_frame, -1), Each(Eq(kSilentFrame)));

  // Now run the second mix. Expect the additional negative frames to be added to the ring.
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_frame, kMixWindowFrames),
              Each(Eq(kPostiveFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_frame, kMixWindowFrames),
              Each(Eq(kNegativeFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

// See discussion on fxrev.dev/641221.
TEST_F(DriverOutputTest, DISABLED_WriteSilenceToRingWhenMuted) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmFormat supportedSampleFormat = {};
  supportedSampleFormat.sample_format = fuchsia::hardware::audio::SampleFormat::PCM_SIGNED;
  supportedSampleFormat.bytes_per_sample = 2;
  supportedSampleFormat.valid_bits_per_sample = 16;
  supportedSampleFormat.number_of_channels = 2;
  supportedSampleFormat.frame_rate = 48000;
  ConfigureDriverForSampleFormat(supportedSampleFormat);

  // 5ms at our chosen sample rate.
  constexpr uint32_t kFifoDepth = 240;
  constexpr zx::duration kExternalDelay = zx::usec(47376);
  driver_->set_fifo_depth(kFifoDepth);
  driver_->set_external_delay(kExternalDelay);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // Mute the output.
  fuchsia::media::AudioGainInfo gain_info;
  gain_info.flags = fuchsia::media::AudioGainInfoFlags::MUTE;
  output_->SetGainInfo(gain_info, fuchsia::media::AudioGainValidFlags::MUTE_VALID);
  RunLoopUntilIdle();

  // Create an add a renderer. We enqueue some audio in this renderer, however we'll expect the
  // ring to only contain silence since the output is muted.
  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(
      dispatcher(), &context().link_matrix(), context().clock_factory());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  bool packet1_released = false;
  bool packet2_released = false;
  renderer->EnqueueAudioPacket(1.0, expected_mix_interval_,
                               [&packet1_released] { packet1_released = true; });
  renderer->EnqueueAudioPacket(-1.0, expected_mix_interval_,
                               [&packet2_released] { packet2_released = true; });

  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  memset(ring_buffer_mapper_.start(), 0xff, RingBufferSizeBytes());

  const uint32_t kMixWindowFrames = 480;
  const uint32_t kSilentFrame = 0;
  const uint32_t kInitialFrame = UINT32_MAX;

  // Renderer clients need to provide packets early, by the amount presentation_delay.
  // Audio data will be mixed into the ring buffer, offset by exactly that amount EXCEPT the
  // external_delay component, which is a post-interconnect delay.
  size_t first_silent_frame = (supportedSampleFormat.frame_rate *
                               (output_->presentation_delay() - kExternalDelay).to_nsecs()) /
                              1'000'000'000;
  size_t num_silent_frames = kMixWindowFrames * 2;

  // Run loop to consume all the frames from the renderer.
  RunLoopFor(expected_mix_interval_);
  RunLoopFor(expected_mix_interval_);
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_silent_frame), Each(Eq(kInitialFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, num_silent_frames),
              Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame + num_silent_frames, -1),
              Each(Eq(kInitialFrame)));

  // Since these packets are mixed they are no longer needed.
  EXPECT_TRUE(packet1_released || packet2_released);

  // Run the loop for |presentation_delay| to verify we release our packets. We add
  // |expected_mix_interval_| - |zx::nsec(1)| to ensure we run the next |Process()| after this lead
  // time has elapsed.
  RunLoopFor((output_->presentation_delay() + expected_mix_interval_ - zx::nsec(1)));
  EXPECT_TRUE(packet1_released);
  EXPECT_TRUE(packet2_released);

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, SelectRateAndChannelizationFromDeviceConfig) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.mutable_bytes_per_sample()->push_back(2);
  formats.mutable_valid_bits_per_sample()->push_back(16);

  // Support the requested rate/channelization from the pipeline config, but also support additional
  // rates and channelizations.

  AddChannelSet(formats, kRequestedDeviceChannels / 2);
  AddChannelSet(formats, kRequestedDeviceChannels);
  AddChannelSet(formats, kRequestedDeviceChannels * 2);

  formats.mutable_frame_rates()->push_back(kRequestedDeviceRate / 2);
  formats.mutable_frame_rates()->push_back(kRequestedDeviceRate);
  formats.mutable_frame_rates()->push_back(kRequestedDeviceRate * 2);
  ConfigureDriverForSampleFormats(std::move(formats));

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // Expect the pipeline to include the 1:2 upchannel effect.
  EXPECT_EQ(1u, output_->pipeline_config()->root().effects_v1.size());
  EXPECT_EQ(output_->pipeline_config()->root().output_channels, kRequestedDeviceChannels / 2);
  EXPECT_EQ(output_->pipeline_config()->root().output_rate, kRequestedDeviceRate);
  auto format = output_->pipeline_config()->OutputFormat(nullptr);
  EXPECT_EQ(format.channels(), kRequestedDeviceChannels);
  EXPECT_EQ(format.frames_per_second(), kRequestedDeviceRate);
}

TEST_F(DriverOutputTest, UseBestAvailableSampleRateAndChannelization) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.mutable_bytes_per_sample()->push_back(2);
  formats.mutable_valid_bits_per_sample()->push_back(16);

  // Support the requested channelization but not the requested sample rate.
  static constexpr int32_t kSupportedFrameRate = kRequestedDeviceRate / 2;
  static constexpr int16_t kSupportedChannels = kRequestedDeviceChannels / 2;
  AddChannelSet(formats, kSupportedChannels);

  formats.mutable_frame_rates()->push_back(kSupportedFrameRate);
  ConfigureDriverForSampleFormats(std::move(formats));

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // If the device does not meet our requirements, then we don't attempt to use the rechannel effect
  // and just rely on our root mix stage to meet the channelization required.
  EXPECT_TRUE(output_->pipeline_config()->root().effects_v1.empty());
  EXPECT_EQ(output_->pipeline_config()->root().output_channels, kSupportedChannels);
  EXPECT_EQ(output_->pipeline_config()->root().output_rate, kSupportedFrameRate);
  auto format = output_->pipeline_config()->OutputFormat(nullptr);
  EXPECT_EQ(format.channels(), kSupportedChannels);
  EXPECT_EQ(format.frames_per_second(), kSupportedFrameRate);
}

}  // namespace
}  // namespace media::audio
