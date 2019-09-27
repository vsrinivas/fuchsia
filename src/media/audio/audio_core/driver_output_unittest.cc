// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/driver_output.h"

#include <lib/fzl/vmo-mapper.h>

#include <fbl/ref_ptr.h>
#include <fbl/span.h>

#include "src/media/audio/audio_core/audio_device_settings_serialization_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_object_registry.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

namespace media::audio {
namespace {
constexpr size_t kRingBufferSizeBytes = 4 * PAGE_SIZE;

class DriverOutputTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    ThreadingModelFixture::SetUp();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

    driver_ = std::make_unique<testing::FakeAudioDriver>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    ASSERT_NE(driver_, nullptr);

    output_ = DriverOutput::Create(std::move(c2), &threading_model(), &object_registry_);
    ASSERT_NE(output_, nullptr);

    ring_buffer_mapper_ = driver_->CreateRingBuffer(kRingBufferSizeBytes);
    ASSERT_NE(ring_buffer<uint8_t>().data(), nullptr);
  }

  // Obtain a view of the ring buffer as a span of a given type. Requires that sizeof(T) be a factor
  // of |kRingBufferSizeBytes|.
  template <typename T>
  fbl::Span<T> ring_buffer() const {
    static_assert(kRingBufferSizeBytes % sizeof(T) == 0);
    return {static_cast<T*>(ring_buffer_mapper_.start()), kRingBufferSizeBytes / sizeof(T)};
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

  testing::FakeObjectRegistry object_registry_;
  std::unique_ptr<testing::FakeAudioDriver> driver_;
  fbl::RefPtr<AudioOutput> output_;
  fzl::VmoMapper ring_buffer_mapper_;
  zx::vmo ring_buffer_;
};

// Simple sanity test that the DriverOutput properly initializes the driver.
TEST_F(DriverOutputTest, DriverOutputStartsDriver) {
  // Fill the ring buffer with some bytes so we can detect if we've written to the buffer.
  auto rb_bytes = ring_buffer<uint8_t>();
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
  auto frames = ring_buffer<uint32_t>();
  const uint32_t kSilentFrame = 0;
  for (size_t i = 0; i < frames.size(); ++i) {
    ASSERT_EQ(frames[i], kSilentFrame);
  }

  threading_model().FidlDomain().ScheduleTask(output_->Shutdown());
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace media::audio
