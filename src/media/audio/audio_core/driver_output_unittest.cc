// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/driver_output.h"

#include <lib/fzl/vmo-mapper.h>

#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_device_settings_serialization_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

using testing::Each;
using testing::Eq;
using testing::FloatEq;

namespace media::audio {
namespace {
constexpr size_t kRingBufferSizeBytes = 8 * PAGE_SIZE;
constexpr zx::duration kExpectedMixInterval =
    DriverOutput::kDefaultHighWaterNsec - DriverOutput::kDefaultLowWaterNsec;

class DriverOutputTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    driver_ = std::make_unique<testing::FakeAudioDriver>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(driver_, nullptr);
    driver_->Start();

    output_ = DriverOutput::Create(std::move(c2), &threading_model(), &device_registry_);
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

  testing::StubDeviceRegistry device_registry_;
  std::unique_ptr<testing::FakeAudioDriver> driver_;
  fbl::RefPtr<AudioOutput> output_;
  fzl::VmoMapper ring_buffer_mapper_;
  zx::vmo ring_buffer_;
};

// Simple sanity test that the DriverOutput properly initializes the driver.
TEST_F(DriverOutputTest, DriverOutputStartsDriver) {
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
  auto selected_format = driver_->selected_format();
  EXPECT_TRUE(selected_format);
  EXPECT_EQ(kSupportedSampleRate, selected_format->frames_per_second);
  EXPECT_EQ(kSupportedSampleFormat, selected_format->sample_format);
  EXPECT_EQ(kSupportedChannels, selected_format->channels);

  // We expect the driver has filled the buffer with silence. For 16-bit/2-channel audio, we can
  // represent each frame as a single uint32_t.
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<float>(), Each(FloatEq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, RendererOutput) {
  // Setup our driver to advertise support for a single format.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher());
  AudioObject::LinkObjects(renderer, output_);
  renderer->EnqueueAudioPacket(1.0, zx::msec(5));
  renderer->EnqueueAudioPacket(1.0, zx::msec(5));

  // Run the loop for just before we expect the mix to occur to validate we're mixing on the correct
  // interval.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  const uint32_t kSilentFrame = 0;
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now run for that last instant and expect a mix has occurred.
  RunLoopFor(zx::nsec(1));
  // Expect 3 sections of the ring:
  //   [0, first_non_silent_frame) - Silence (corresponds to the mix lead time.
  //   [first_non_silent_frame, first_silent_frame) - Non silent samples, corresponds to the 1.0
  //       samples provided by the renderer. This corresponds to 0x7fff in uint16.
  //   [first_silent_frame, ring_buffer.size()) - Silence again (we did not provide any data to
  //       mix at this point in the ring buffer.
  const uint32_t kNonSilentFrame = 0x7fff7fff;
  const uint32_t kMixWindowFrames = 480;
  size_t first_non_silent_frame =
      (kSupportedSampleRate * output_->min_lead_time().to_nsecs()) / 1'000'000'000;
  size_t first_silent_frame = first_non_silent_frame + kMixWindowFrames;

  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_non_silent_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_non_silent_frame, kMixWindowFrames),
              Each(Eq(kNonSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

TEST_F(DriverOutputTest, MixAtExpectedInterval) {
  // Setup our driver to advertise support for a single format.
  constexpr uint8_t kSupportedChannels = 2;
  constexpr uint32_t kSupportedSampleRate = 48000;
  constexpr audio_sample_format_t kSupportedSampleFormat = AUDIO_SAMPLE_FORMAT_16BIT;
  // 5ms at our chosen sample rate.
  constexpr uint32_t kFifoDepth = 240;
  driver_->set_fifo_depth(kFifoDepth);
  ConfigureDriverForSampleFormat(kSupportedChannels, kSupportedSampleRate, kSupportedSampleFormat,
                                 ASF_RANGE_FLAG_FPS_48000_FAMILY);

  threading_model().FidlDomain().ScheduleTask(output_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(driver_->is_running());

  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(dispatcher());
  AudioObject::LinkObjects(renderer, output_);
  renderer->EnqueueAudioPacket(1.0, kExpectedMixInterval);
  renderer->EnqueueAudioPacket(-1.0, kExpectedMixInterval);

  // We'll have 4 sections in our ring buffer:
  //   > Silence during the initial lead time.
  //   > 10ms of frames that contain 1.0 float data.
  //   > 10ms of frames that contain -1.0 float data.
  //   > Silence during the rest of the ring.
  const uint32_t kSilentFrame = 0;
  const uint32_t kPostiveOneSamples = 0x7fff7fff;
  const uint32_t kNegativeOneSamples = 0x80008000;
  const uint32_t kMixWindowFrames = 480;
  size_t first_positive_one_frame =
      (kSupportedSampleRate * output_->min_lead_time().to_nsecs()) / 1'000'000'000;
  size_t first_negative_one_frame = first_positive_one_frame + kMixWindowFrames;
  size_t first_silent_frame = first_negative_one_frame + kMixWindowFrames;

  // Run until just before the expected first mix. Expect the ring buffer to be empty.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  EXPECT_THAT(RingBuffer<uint32_t>(), Each(Eq(kSilentFrame)));

  // Now expect the first mix, which adds the 1.0 samples
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_one_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_one_frame, kMixWindowFrames),
              Each(Eq(kPostiveOneSamples)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_one_frame, kMixWindowFrames),
              Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));

  // Run until just before the next mix interval. Expect the ring to be unchanged.
  RunLoopFor(kExpectedMixInterval - zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_one_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_one_frame, kMixWindowFrames),
              Each(Eq(kPostiveOneSamples)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_one_frame, kMixWindowFrames),
              Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));

  // Now run the second mix. Expect the rest of the frames to be in the ring.
  RunLoopFor(zx::nsec(1));
  EXPECT_THAT(RingBufferSlice<uint32_t>(0, first_positive_one_frame), Each(Eq(kSilentFrame)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_positive_one_frame, kMixWindowFrames),
              Each(Eq(kPostiveOneSamples)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_negative_one_frame, kMixWindowFrames),
              Each(Eq(kNegativeOneSamples)));
  EXPECT_THAT(RingBufferSlice<uint32_t>(first_silent_frame, -1), Each(Eq(kSilentFrame)));

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace media::audio
