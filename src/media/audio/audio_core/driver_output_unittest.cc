// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/driver_output.h"

#include <lib/fzl/vmo-mapper.h>
#include <zircon/status.h>

#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/loudness_transform.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"
#include "src/media/audio/lib/format/driver_format.h"
#include "src/media/audio/lib/logging/logging.h"

using testing::Each;
using testing::Eq;
using testing::FloatEq;

namespace media::audio {
namespace {
constexpr size_t kRingBufferSizeBytes = 8 * PAGE_SIZE;
constexpr zx::duration kExpectedMixInterval =
    DriverOutput::kDefaultHighWaterNsec - DriverOutput::kDefaultLowWaterNsec;
constexpr zx::duration kBeyondSubmittedPackets = zx::sec(1);

const fuchsia::media::AudioStreamType kDefaultStreamType{
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
};

}  // namespace

class DriverOutputTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    driver_ = std::make_unique<testing::FakeAudioDriverV1>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(driver_, nullptr);

    output_ = std::make_shared<DriverOutput>("", &threading_model(), &context().device_manager(),
                                             std::move(c2), &context().link_matrix(),
                                             context().process_config().default_volume_curve());
    ASSERT_NE(output_, nullptr);

    ring_buffer_mapper_ = driver_->CreateRingBuffer(kRingBufferSizeBytes);
    ASSERT_NE(ring_buffer_mapper_.start(), nullptr);
  }

  // Use len = -1 for the end of the buffer.
  template <typename T>
  fbl::Span<T> RingBufferSlice(size_t first, ssize_t maybe_len) const {
    static_assert(kRingBufferSizeBytes % sizeof(T) == 0);
    T* array = static_cast<T*>(ring_buffer_mapper_.start());
    size_t len = maybe_len >= 0 ? maybe_len : (kRingBufferSizeBytes / sizeof(T)) - first;
    FX_CHECK((first + len) * sizeof(T) <= kRingBufferSizeBytes);
    return {&array[first], len};
  }

  template <typename T>
  std::array<T, kRingBufferSizeBytes / sizeof(T)>& RingBuffer() const {
    static_assert(kRingBufferSizeBytes % sizeof(T) == 0);
    return reinterpret_cast<std::array<T, kRingBufferSizeBytes / sizeof(T)>&>(
        static_cast<T*>(ring_buffer_mapper_.start())[0]);
  }

  // Updates the driver to advertise the given format. This will be the only audio format that the
  // driver exposes.
  void ConfigureDriverForSampleFormat(uint8_t chans, uint32_t sample_rate,
                                      audio_sample_format_t sample_format, uint16_t flags) {
    driver_->set_formats({{
        .sample_formats = sample_format,
        .min_frames_per_second = sample_rate,
        .max_frames_per_second = sample_rate,
        .min_channels = chans,
        .max_channels = chans,
        .flags = flags,
    }});
  }

  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  std::unique_ptr<testing::FakeAudioDriverV1> driver_;
  std::shared_ptr<AudioOutput> output_;
  fzl::VmoMapper ring_buffer_mapper_;
  zx::vmo ring_buffer_;
};

// Simple sanity test that the DriverOutput properly initializes the driver.
TEST_F(DriverOutputTest, DriverOutputStartsDriver) {
  driver_->Start();
  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  auto rb_bytes = RingBuffer<uint8_t>();
  memset(rb_bytes.data(), 0xff, rb_bytes.size());

  // Setup our driver to advertise support for only 16-bit/2-channel/48khz audio.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

  // Startup the DriverOutput. We expect it's completed some basic initialization of the driver.
  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // Verify the DriverOutput has requested a ring buffer with the correct format type. Since we only
  // published support for a single format above, there's only one possible solution here.
  auto selected_format = output_->format();
  EXPECT_TRUE(selected_format);
  EXPECT_EQ(kSupportedSampleRate, selected_format->frames_per_second());
  EXPECT_EQ(kSupportedChannels, selected_format->channels());

  audio_sample_format_t selected_sample_format;
  ASSERT_TRUE(AudioSampleFormatToDriverSampleFormat(selected_format->sample_format(),
                                                    &selected_sample_format));
  EXPECT_EQ(kSupportedSampleFormat, selected_sample_format);

  // We expect the driver has filled the buffer with silence. For 16-bit/2-channel audio, we can
  // represent each frame as a single uint32_t.
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<float>(), Each(FloatEq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

// Test fix for fxbug.dev/47251
TEST_F(DriverOutputTest, HandlePlugDetectBeforeStartResponse) {
  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  auto rb_bytes = RingBuffer<uint8_t>();
  memset(rb_bytes.data(), 0xff, rb_bytes.size());

  // Setup our driver to advertise support for only 16-bit/2-channel/48khz audio.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);
  driver_->set_plugged(true);
  driver_->set_hardwired(false);

  // Startup the DriverOutput. We expect it's completed some basic initialization of the driver.
  context().device_manager().AddDevice(output_);
  RunLoopUntilIdle();

  // We want to step through some driver commands to that we can send the plug detect message before
  // the ring buffer is started.
  while (true) {
    auto result = driver_->Step();
    RunLoopUntilIdle();
    if (!result.is_ok()) {
      ASSERT_EQ(result.error(), ZX_ERR_SHOULD_WAIT)
          << "Command filed " << zx_status_get_string(result.error());
      break;
    }
  }

  // |AUDIO_RB_CMD_GET_BUFFER| comes right before |START|, so we'll stop processing those messages
  // then.
  while (true) {
    auto result = driver_->StepRingBuffer();
    ASSERT_TRUE(result.is_ok()) << "Command failed " << zx_status_get_string(result.error());
    RunLoopUntilIdle();
    if (result.value() == AUDIO_RB_CMD_GET_BUFFER) {
      break;
    }
  }

  // Now process the main channel again. This will process any plug detect messages.
  while (true) {
    auto result = driver_->Step();
    RunLoopUntilIdle();
    if (!result.is_ok()) {
      ASSERT_EQ(result.error(), ZX_ERR_SHOULD_WAIT)
          << "Command filed " << zx_status_get_string(result.error());
      break;
    }
  }

  // Now add a renderer. We expect it to not yet be linked because the ring buffer hasn't completed
  // the |AUDIO_RB_CMD_START| message yet.
  auto renderer = testing::FakeAudioRenderer::Create(
      dispatcher(), std::make_optional<Format>(Format::Create(kDefaultStreamType).take_value()),
      fuchsia::media::AudioRenderUsage::MEDIA, &context().link_matrix());
  context().route_graph().AddRenderer(renderer);
  context().route_graph().SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  RunLoopUntilIdle();

  // Since the output is not started, we should not have linked the renderer yet.
  ASSERT_FALSE(context().link_matrix().AreLinked(*renderer, *output_));

  // Now finish starting the ring buffer and confirm the link has been made to our renderer.
  auto result = driver_->StepRingBuffer();
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), AUDIO_RB_CMD_START);
  result = driver_->StepRingBuffer();
  ASSERT_FALSE(result.is_ok());
  ASSERT_TRUE(context().link_matrix().AreLinked(*renderer, *output_));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, RendererOutput) {
  driver_->Start();
  // Setup our driver to advertise support for a single format.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  renderer->EnqueueAudioPacket(0.5, zx::msec(5));
  renderer->EnqueueAudioPacket(0.5, zx::msec(5));
  // Only these first two packets will be mixed -- we'll stop before mixing the third.
  bool packet3_released = false;
  renderer->EnqueueAudioPacket(-1.0, zx::msec(5), [&packet3_released] { packet3_released = true; });

  // Run the loop to just before the mix should occur, to validate we mix on the correct interval.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now run for that last instant and expect a mix has occurred.
  RunLoopFor(zx::nsec(1));
  // Expect 3 sections of the ring:
  //   [0, first_non_silent_frame) - Silence (corresponds to the mix lead time).
  //   [first_non_silent_frame, first_silent_frame) - Non silent samples (corresponds to 0.5
  //       samples provided by the renderer, which is 0x4000 in uint16).
  //   [first_silent_frame, ring_buffer.size()) - Silence again (we did not provide any data to
  //       mix at this point in the ring buffer).
  const uint32_t kNonSilentFrame = 0x40004000;
  const uint32_t kMixWindowFrames = 480;
  size_t first_non_silent_frame =
      (kSupportedSampleRate * output_->presentation_delay().to_nsecs()) / 1'000'000'000;
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
  driver_->Start();
  // Setup our driver to advertise support for a single format.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;

  // 5ms at our chosen sample rate.
  constexpr uint32_t kFifoDepth = 240;
  constexpr zx::duration kExternalDelay = zx::usec(47376);
  driver_->set_fifo_depth(kFifoDepth);
  driver_->set_external_delay(kExternalDelay);
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  renderer->EnqueueAudioPacket(0.875, kExpectedMixInterval);
  renderer->EnqueueAudioPacket(-0.875, kExpectedMixInterval);

  // We'll have 4 sections in our ring buffer:
  //  *  Silence during the initial lead time.
  //  *  10ms of frames that contain 0.875 float data.
  //  *  10ms of frames that contain -0.875 float data.
  //  *  Silence during the rest of the ring.
  const uint32_t kSilentFrame = 0;
  const uint32_t kPostiveFrame = 0x70007000;
  const uint32_t kNegativeFrame = 0x90009000;
  const uint32_t kMixWindowFrames = 480;

  // Renderer clients need to provide packets early, by the amount presentation_delay.
  // Audio data will be mixed into the ring buffer, offset by exactly that amount EXCEPT the
  // external_delay component, which is a post-interconnect delay.
  size_t first_positive_frame =
      (kSupportedSampleRate * (output_->presentation_delay() - kExternalDelay).to_nsecs()) /
      1'000'000'000;
  size_t first_negative_frame = first_positive_frame + kMixWindowFrames;
  size_t first_silent_frame = first_negative_frame + kMixWindowFrames;

  // Run until just before the expected first mix. Expect the ring buffer to be empty.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now expect the first mix, which adds the 0.875 samples
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_frame, kMixWindowFrames),
              Each(Eq(kPostiveFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_frame, -1), Each(Eq(kSilentFrame)));

  // Run until just before the next mix interval. Expect the ring to be unchanged.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
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

TEST_F(DriverOutputTest, WriteSilenceToRingWhenMuted) {
  driver_->Start();
  // Setup our driver to advertise support for a single format.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;

  // 5ms at our chosen sample rate.
  constexpr uint32_t kFifoDepth = 240;
  constexpr zx::duration kExternalDelay = zx::usec(47376);
  driver_->set_fifo_depth(kFifoDepth);
  driver_->set_external_delay(kExternalDelay);
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

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
  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  bool packet1_released = false;
  bool packet2_released = false;
  renderer->EnqueueAudioPacket(1.0, kExpectedMixInterval,
                               [&packet1_released] { packet1_released = true; });
  renderer->EnqueueAudioPacket(-1.0, kExpectedMixInterval,
                               [&packet2_released] { packet2_released = true; });

  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  memset(ring_buffer_mapper_.start(), 0xff, kRingBufferSizeBytes);

  const uint32_t kMixWindowFrames = 480;
  const uint32_t kSilentFrame = 0;
  const uint32_t kInitialFrame = UINT32_MAX;

  // Renderer clients need to provide packets early, by the amount presentation_delay.
  // Audio data will be mixed into the ring buffer, offset by exactly that amount EXCEPT the
  // external_delay component, which is a post-interconnect delay.
  size_t first_silent_frame =
      (kSupportedSampleRate * (output_->presentation_delay() - kExternalDelay).to_nsecs()) /
      1'000'000'000;
  size_t num_silent_frames = kMixWindowFrames * 2;

  // Run loop to consume all the frames from the renderer.
  RunLoopFor(kExpectedMixInterval);
  RunLoopFor(kExpectedMixInterval);
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_silent_frame), Each(Eq(kInitialFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, num_silent_frames),
              Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame + num_silent_frames, -1),
              Each(Eq(kInitialFrame)));

  // We expect to have mixed these packets, but we want to hold onto them until the corresponding
  // frames would have been played back.
  EXPECT_FALSE(packet1_released || packet2_released);

  // Run the loop for |presentation_delay| to verify we release our packets. We add
  // |kExpectedMixInterval| - |zx::nsec(1)| to ensure we run the next |Process()| after this lead
  // time has elapsed.
  RunLoopFor((output_->presentation_delay() + kExpectedMixInterval - zx::nsec(1)));
  EXPECT_TRUE(packet1_released);
  EXPECT_TRUE(packet2_released);

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

class DriverV2OutputTest : public testing::ThreadingModelFixture {
 protected:
  static constexpr uint32_t kRequestedDeviceRate = 48000;
  static constexpr uint16_t kRequestedDeviceChannels = 4;
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
    config.mutable_root().effects = {{
        .lib_name = testing::kTestEffectsModuleName,
        .effect_name = "rechannel",
        .instance_name = "1:2 upchannel",
        .effect_config = "",
        .output_channels = kRequestedDeviceChannels,
    }};
    return config;
  }

  DriverV2OutputTest()
      : ThreadingModelFixture(
            ProcessConfig::Builder()
                .AddDeviceProfile(
                    {std::nullopt,
                     DeviceConfig::OutputDeviceProfile(
                         /* eligible_for_loopback */ true,
                         StreamUsageSetFromRenderUsages(kFidlRenderUsages),
                         VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                         /* independent_volume_control */ false, CreatePipelineConfig(),
                         /* driver_gain_db */ 0.0)})
                .SetDefaultVolumeCurve(
                    VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                .Build()) {}

  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    driver_ = std::make_unique<testing::FakeAudioDriverV2>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(driver_, nullptr);
    driver_->Start();

    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
    stream_config.set_channel(std::move(c2));
    output_ = std::make_shared<DriverOutput>("", &threading_model(), &context().device_manager(),
                                             std::move(stream_config), &context().link_matrix(),
                                             context().process_config().default_volume_curve());
    ASSERT_NE(output_, nullptr);

    ring_buffer_mapper_ = driver_->CreateRingBuffer(kRingBufferSizeBytes);
    ASSERT_NE(ring_buffer_mapper_.start(), nullptr);

    // Add a rechannel effect.
    test_effects_.AddEffect("rechannel")
        .WithChannelization(FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY, FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY)
        .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  }

  // Use len = -1 for the end of the buffer.
  template <typename T>
  fbl::Span<T> RingBufferSlice(size_t first, ssize_t maybe_len) const {
    static_assert(kRingBufferSizeBytes % sizeof(T) == 0);
    T* array = static_cast<T*>(ring_buffer_mapper_.start());
    size_t len = maybe_len >= 0 ? maybe_len : (kRingBufferSizeBytes / sizeof(T)) - first;
    FX_CHECK((first + len) * sizeof(T) <= kRingBufferSizeBytes);
    return {&array[first], len};
  }

  template <typename T>
  std::array<T, kRingBufferSizeBytes / sizeof(T)>& RingBuffer() const {
    static_assert(kRingBufferSizeBytes % sizeof(T) == 0);
    return reinterpret_cast<std::array<T, kRingBufferSizeBytes / sizeof(T)>&>(
        static_cast<T*>(ring_buffer_mapper_.start())[0]);
  }

  // Updates the driver to advertise the given format. This will be the only audio format that the
  // driver exposes.
  void ConfigureDriverForSampleFormat(fuchsia::hardware::audio::PcmFormat sample_format) {
    fuchsia::hardware::audio::PcmSupportedFormats formats = {};
    formats.number_of_channels.push_back(sample_format.number_of_channels);
    formats.sample_formats.push_back(sample_format.sample_format);
    formats.bytes_per_sample.push_back(sample_format.bytes_per_sample);
    formats.valid_bits_per_sample.push_back(sample_format.valid_bits_per_sample);
    formats.frame_rates.push_back(sample_format.frame_rate);
    ConfigureDriverForSampleFormats(std::move(formats));
  }
  void ConfigureDriverForSampleFormats(fuchsia::hardware::audio::PcmSupportedFormats formats) {
    driver_->set_formats(std::move(formats));
  }

  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  std::unique_ptr<testing::FakeAudioDriverV2> driver_;
  std::shared_ptr<DriverOutput> output_;
  fzl::VmoMapper ring_buffer_mapper_;
  zx::vmo ring_buffer_;
  testing::TestEffectsModule test_effects_ = testing::TestEffectsModule::Open();
};

// Simple sanity test that the DriverOutput properly initializes the driver.
TEST_F(DriverV2OutputTest, DriverOutputStartsDriver) {
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

TEST_F(DriverV2OutputTest, RendererOutput) {
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

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
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
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
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

TEST_F(DriverV2OutputTest, MixAtExpectedInterval) {
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

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  renderer->EnqueueAudioPacket(0.75, kExpectedMixInterval);
  renderer->EnqueueAudioPacket(-0.75, kExpectedMixInterval);

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
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now expect the first mix, which adds the positive samples
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_frame, kMixWindowFrames),
              Each(Eq(kPostiveFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_frame, -1), Each(Eq(kSilentFrame)));

  // Run until just before the next mix interval. Expect the ring to be unchanged.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
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

TEST_F(DriverV2OutputTest, WriteSilenceToRingWhenMuted) {
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
  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher(),
                                                                          &context().link_matrix());
  context().link_matrix().LinkObjects(renderer, output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));
  bool packet1_released = false;
  bool packet2_released = false;
  renderer->EnqueueAudioPacket(1.0, kExpectedMixInterval,
                               [&packet1_released] { packet1_released = true; });
  renderer->EnqueueAudioPacket(-1.0, kExpectedMixInterval,
                               [&packet2_released] { packet2_released = true; });

  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  memset(ring_buffer_mapper_.start(), 0xff, kRingBufferSizeBytes);

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
  RunLoopFor(kExpectedMixInterval);
  RunLoopFor(kExpectedMixInterval);
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_silent_frame), Each(Eq(kInitialFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, num_silent_frames),
              Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame + num_silent_frames, -1),
              Each(Eq(kInitialFrame)));

  // We expect to have mixed these packets, but we want to hold onto them until the corresponding
  // frames would have been played back.
  EXPECT_FALSE(packet1_released || packet2_released);

  // Run the loop for |presentation_delay| to verify we release our packets. We add
  // |kExpectedMixInterval| - |zx::nsec(1)| to ensure we run the next |Process()| after this lead
  // time has elapsed.
  RunLoopFor((output_->presentation_delay() + kExpectedMixInterval - zx::nsec(1)));
  EXPECT_TRUE(packet1_released);
  EXPECT_TRUE(packet2_released);

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverV2OutputTest, SelectRateAndChannelizationFromDeviceConfig) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  formats.sample_formats.push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.bytes_per_sample.push_back(2);
  formats.valid_bits_per_sample.push_back(16);

  // Support the requested rate/channelization from the pipeline config, but also support additional
  // rates and channelizations.
  formats.number_of_channels.push_back(kRequestedDeviceChannels / 2);
  formats.number_of_channels.push_back(kRequestedDeviceChannels);
  formats.number_of_channels.push_back(kRequestedDeviceChannels * 2);
  formats.frame_rates.push_back(kRequestedDeviceRate / 2);
  formats.frame_rates.push_back(kRequestedDeviceRate);
  formats.frame_rates.push_back(kRequestedDeviceRate * 2);
  ConfigureDriverForSampleFormats(std::move(formats));

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // Expect the pipeline to include the 1:2 upchannel effect.
  EXPECT_EQ(1u, output_->pipeline_config().root().effects.size());
  EXPECT_EQ(output_->pipeline_config().root().output_channels, kRequestedDeviceChannels / 2);
  EXPECT_EQ(output_->pipeline_config().root().output_rate, kRequestedDeviceRate);
  EXPECT_EQ(output_->pipeline_config().channels(), kRequestedDeviceChannels);
  EXPECT_EQ(output_->pipeline_config().frames_per_second(), kRequestedDeviceRate);
}

TEST_F(DriverV2OutputTest, UseBestAvailableSampleRateAndChannelization) {
  // Setup our driver to advertise support for a single format.
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  formats.sample_formats.push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats.bytes_per_sample.push_back(2);
  formats.valid_bits_per_sample.push_back(16);

  // Support the requested channelization but not the requested sample rate.
  static constexpr uint32_t kSupportedFrameRate = kRequestedDeviceRate / 2;
  static constexpr uint16_t kSupportedChannels = kRequestedDeviceChannels / 2;
  formats.number_of_channels.push_back(kSupportedChannels);
  formats.frame_rates.push_back(kSupportedFrameRate);
  ConfigureDriverForSampleFormats(std::move(formats));

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  // If the device does not meet our requirements, then we don't attempt to use the rechannel effect
  // and just rely on our root mix stage to meet the channelization required.
  EXPECT_TRUE(output_->pipeline_config().root().effects.empty());
  EXPECT_EQ(output_->pipeline_config().root().output_channels, kSupportedChannels);
  EXPECT_EQ(output_->pipeline_config().root().output_rate, kSupportedFrameRate);
  EXPECT_EQ(output_->pipeline_config().channels(), kSupportedChannels);
  EXPECT_EQ(output_->pipeline_config().frames_per_second(), kSupportedFrameRate);
}

}  // namespace media::audio
