// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mix_stage.h"

#include <zircon/syscalls.h>

#include <fbl/string_printf.h>
#include <gmock/gmock.h>

#include "ffl/string.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/ring_buffer.h"
#include "src/media/audio/audio_core/testing/fake_stream.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/format/constants.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {

namespace {
enum class ClockMode { SAME, WITH_OFFSET, RATE_ADJUST };

constexpr uint32_t kDefaultNumChannels = 2;
constexpr uint32_t kDefaultFrameRate = 48000;
const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = kDefaultNumChannels,
                       .frames_per_second = kDefaultFrameRate,
                   })
        .take_value();
}  // namespace

class MixStageTest : public testing::ThreadingModelFixture {
 protected:
  static constexpr uint32_t kBlockSizeFrames = 240;

  void SetUp() {
    zx::clock zx_device_clock = clock::CloneOfMonotonic();
    auto clock_result = audio::clock::DuplicateClock(zx_device_clock);
    ASSERT_TRUE(clock_result.is_ok());
    zx::clock zx_clone_device_clock = clock_result.take_value();

    device_clock_ = context().clock_factory()->CreateDeviceFixed(std::move(zx_device_clock),
                                                                 AudioClock::kMonotonicDomain);
    clone_of_device_clock_ = context().clock_factory()->CreateDeviceFixed(
        std::move(zx_clone_device_clock), AudioClock::kMonotonicDomain);

    mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kBlockSizeFrames, timeline_function_,
                                            *device_clock_);
  }

  int64_t duration_to_frames(zx::duration delta) {
    return kDefaultFormat.frames_per_ns().Scale(delta.to_nsecs());
  }

  fbl::RefPtr<VersionedTimelineFunction> timeline_function_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  // Views the memory at |ptr| as a std::array of |N| elements of |T|. If |offset| is provided, it
  // is the number of |T| sized elements to skip at the beginning of |ptr|.
  //
  // It is entirely up to the caller to ensure that values of |T|, |N|, and |offset| are chosen to
  // not overflow |ptr|.
  template <typename T, size_t N>
  std::array<T, N>& as_array(void* ptr, size_t offset = 0) {
    return reinterpret_cast<std::array<T, N>&>(static_cast<T*>(ptr)[offset]);
  }

  std::unique_ptr<AudioClock> SetPacketFactoryWithOffsetAudioClock(zx::duration clock_offset,
                                                                   testing::PacketFactory& factory);
  void TestMixStageTrim(ClockMode clock_mode);
  void TestMixStageUniformFormats(ClockMode clock_mode);
  void TestMixStageSingleInput(ClockMode clock_mode);

  void ValidateIsPointSampler(std::shared_ptr<Mixer> should_be_point) {
    EXPECT_LT(should_be_point->pos_filter_width(), Fixed(1))
        << "Mixer pos_filter_width " << should_be_point->pos_filter_width().raw_value()
        << " too large, should be less than " << Fixed(1).raw_value();
  }

  void ValidateIsSincSampler(std::shared_ptr<Mixer> should_be_sinc) {
    EXPECT_GT(should_be_sinc->pos_filter_width(), Fixed(1))
        << "Mixer pos_filter_width " << should_be_sinc->pos_filter_width().raw_value()
        << " too small, should be greater than " << Fixed(1).raw_value();
  }

  std::shared_ptr<MixStage> mix_stage_;

  std::unique_ptr<AudioClock> device_clock_;
  std::unique_ptr<AudioClock> clone_of_device_clock_;
};

TEST_F(MixStageTest, AddInput_MixerSelection) {
  const Format kSameFrameRate =
      Format::Create(fuchsia::media::AudioStreamType{
                         .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                         .channels = 1,
                         .frames_per_second = kDefaultFrameRate,
                     })
          .take_value();

  const Format kDiffFrameRate =
      Format::Create(fuchsia::media::AudioStreamType{
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = kDefaultNumChannels,
                         .frames_per_second = kDefaultFrameRate / 2,
                     })
          .take_value();

  auto timeline = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto tl_same = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kSameFrameRate.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto tl_different = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDiffFrameRate.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  auto adjustable_device_clock = context().clock_factory()->CreateDeviceAdjustable(
      clock::AdjustableCloneOfMonotonic(), AudioClock::kMonotonicDomain + 1);
  auto adjustable_device_mix_stage = std::make_shared<MixStage>(kDefaultFormat, kBlockSizeFrames,
                                                                timeline, *adjustable_device_clock);
  auto fixed_device_clock = context().clock_factory()->CreateDeviceFixed(
      clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);
  auto fixed_device_mix_stage =
      std::make_shared<MixStage>(kDefaultFormat, kBlockSizeFrames, timeline, *fixed_device_clock);

  auto adjustable_client_same_rate = std::make_shared<PacketQueue>(
      kSameFrameRate, tl_same,
      context().clock_factory()->CreateClientAdjustable(clock::AdjustableCloneOfMonotonic()));
  auto adjustable_client_diff_rate = std::make_shared<PacketQueue>(
      kDiffFrameRate, tl_different,
      context().clock_factory()->CreateClientAdjustable(clock::AdjustableCloneOfMonotonic()));
  auto custom_same_rate = std::make_shared<PacketQueue>(
      kSameFrameRate, tl_same,
      context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));

  // client adjustable should lead to Point, if same rate
  ValidateIsPointSampler(adjustable_device_mix_stage->AddInput(adjustable_client_same_rate));
  ValidateIsPointSampler(fixed_device_mix_stage->AddInput(adjustable_client_same_rate));

  // client adjustable should lead to Sinc, if not same rate
  ValidateIsSincSampler(adjustable_device_mix_stage->AddInput(adjustable_client_diff_rate));
  ValidateIsSincSampler(fixed_device_mix_stage->AddInput(adjustable_client_diff_rate));

  // custom clock should lead to Sinc, even if same rate, regardless of hardware-control
  ValidateIsSincSampler(adjustable_device_mix_stage->AddInput(custom_same_rate));
  ValidateIsSincSampler(fixed_device_mix_stage->AddInput(custom_same_rate));

  // The default heuristic can still be explicitly indicated, and behaves as above.
  ValidateIsPointSampler(adjustable_device_mix_stage->AddInput(
      adjustable_client_same_rate, std::nullopt, Mixer::Resampler::Default));
  ValidateIsPointSampler(fixed_device_mix_stage->AddInput(adjustable_client_same_rate, std::nullopt,
                                                          Mixer::Resampler::Default));
  ValidateIsSincSampler(adjustable_device_mix_stage->AddInput(
      adjustable_client_diff_rate, std::nullopt, Mixer::Resampler::Default));
  ValidateIsSincSampler(fixed_device_mix_stage->AddInput(adjustable_client_diff_rate, std::nullopt,
                                                         Mixer::Resampler::Default));
  ValidateIsSincSampler(adjustable_device_mix_stage->AddInput(custom_same_rate, std::nullopt,
                                                              Mixer::Resampler::Default));
  ValidateIsSincSampler(
      fixed_device_mix_stage->AddInput(custom_same_rate, std::nullopt, Mixer::Resampler::Default));

  //
  // For all, explicit mixer selection can still countermand our default heuristic
  //
  // WindowedSinc can still be explicitly specified in same-rate no-microSRC situations
  ValidateIsSincSampler(adjustable_device_mix_stage->AddInput(
      adjustable_client_same_rate, std::nullopt, Mixer::Resampler::WindowedSinc));
  ValidateIsSincSampler(fixed_device_mix_stage->AddInput(adjustable_client_same_rate, std::nullopt,
                                                         Mixer::Resampler::WindowedSinc));

  // SampleAndHold can still be explicitly specified, even in different-rate situations
  ValidateIsPointSampler(adjustable_device_mix_stage->AddInput(
      adjustable_client_diff_rate, std::nullopt, Mixer::Resampler::SampleAndHold));
  ValidateIsPointSampler(fixed_device_mix_stage->AddInput(adjustable_client_diff_rate, std::nullopt,
                                                          Mixer::Resampler::SampleAndHold));

  // SampleAndHold can still be explicitly specified, even in microSRC situations
  ValidateIsPointSampler(adjustable_device_mix_stage->AddInput(custom_same_rate, std::nullopt,
                                                               Mixer::Resampler::SampleAndHold));
  ValidateIsPointSampler(fixed_device_mix_stage->AddInput(custom_same_rate, std::nullopt,
                                                          Mixer::Resampler::SampleAndHold));
}

// TODO(fxbug.dev/50004): Add tests to verify we can read from other mix stages with unaligned
// frames.

std::unique_ptr<AudioClock> MixStageTest::SetPacketFactoryWithOffsetAudioClock(
    zx::duration clock_offset, testing::PacketFactory& factory) {
  auto custom_clock =
      clock::testing::CreateCustomClock({.start_val = zx::clock::get_monotonic() + clock_offset})
          .take_value();

  auto actual_offset = clock::testing::GetOffsetFromMonotonic(custom_clock).take_value();

  int64_t seek_frame = round(
      static_cast<double>(kDefaultFormat.frames_per_second() * actual_offset.get()) / ZX_SEC(1));
  factory.SeekToFrame(seek_frame);

  return context().clock_factory()->CreateClientFixed(std::move(custom_clock));
}

void MixStageTest::TestMixStageTrim(ClockMode clock_mode) {
  // Set timeline rate to match our format.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  std::shared_ptr<PacketQueue> packet_queue;
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, zx_system_get_page_size());

  if (clock_mode == ClockMode::SAME) {
    packet_queue = std::make_shared<PacketQueue>(
        kDefaultFormat, timeline_function,
        context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));
  } else if (clock_mode == ClockMode::WITH_OFFSET) {
    auto custom_audio_clock = SetPacketFactoryWithOffsetAudioClock(zx::sec(-2), packet_factory);

    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function,
                                                 std::move(custom_audio_clock));
  } else {
    ASSERT_TRUE(clock_mode == ClockMode::RATE_ADJUST) << "Unknown clock mode";
    ASSERT_TRUE(false) << "Multi-rate testing not yet implemented";
  }

  mix_stage_->AddInput(packet_queue);

  bool packet1_released = false;
  bool packet2_released = false;

  packet_queue->PushPacket(packet_factory.CreatePacket(
      1.0, zx::msec(5), [&packet1_released] { packet1_released = true; }));
  packet_queue->PushPacket(packet_factory.CreatePacket(
      0.5, zx::msec(5), [&packet2_released] { packet2_released = true; }));

  // Because of how we set up custom clocks, we can't reliably Trim to a specific frame number (we
  // might be off by half a frame), so we allow ourselves one frame of tolerance either direction.
  constexpr int64_t kToleranceFrames = 1;

  // Before 5ms: packet1 is not yet entirely consumed; we should still retain both packets.
  mix_stage_->Trim(Fixed(duration_to_frames(zx::msec(5)) - kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_FALSE(packet1_released);

  // After 5ms: packet1 is consumed and should have been released. We should still retain packet2.
  mix_stage_->Trim(Fixed(duration_to_frames(zx::msec(5)) + kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_TRUE(packet1_released);
  EXPECT_FALSE(packet2_released);

  // Before 10ms: packet2 is not yet entirely consumed; we should still retain it.
  mix_stage_->Trim(Fixed(duration_to_frames(zx::msec(10)) - kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_FALSE(packet2_released);

  // After 10ms: packet2 is consumed and should have been released.
  mix_stage_->Trim(Fixed(duration_to_frames(zx::msec(10)) + kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_TRUE(packet2_released);

  // Upon any fail, slab_allocator asserts at exit. Clear all allocations, so testing can continue.
  mix_stage_->Trim(Fixed::Max());
}

TEST_F(MixStageTest, Trim) { TestMixStageTrim(ClockMode::SAME); }
TEST_F(MixStageTest, Trim_ClockOffset) { TestMixStageTrim(ClockMode::WITH_OFFSET); }

void MixStageTest::TestMixStageUniformFormats(ClockMode clock_mode) {
  // Set timeline rate to match our format.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  // Create 2 PacketQueues that we mix together. One may have a clock with an offset, so create a
  // seperate PacketFactory for it, that can set timestamps appropriately.
  testing::PacketFactory packet_factory1(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  testing::PacketFactory packet_factory2(dispatcher(), kDefaultFormat, zx_system_get_page_size());

  auto packet_queue1 = std::make_shared<PacketQueue>(
      kDefaultFormat, timeline_function,
      context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));
  std::shared_ptr<PacketQueue> packet_queue2;

  if (clock_mode == ClockMode::SAME) {
    packet_queue2 = std::make_shared<PacketQueue>(
        kDefaultFormat, timeline_function,
        context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));
  } else if (clock_mode == ClockMode::WITH_OFFSET) {
    auto custom_audio_clock = SetPacketFactoryWithOffsetAudioClock(zx::sec(10), packet_factory2);

    packet_queue2 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function,
                                                  std::move(custom_audio_clock));
  } else {
    ASSERT_TRUE(clock_mode == ClockMode::RATE_ADJUST) << "Unknown clock mode";
    ASSERT_TRUE(false) << "Multi-rate testing not yet implemented";
  }

  mix_stage_->AddInput(packet_queue1, std::nullopt, Mixer::Resampler::SampleAndHold);
  mix_stage_->AddInput(packet_queue2, std::nullopt, Mixer::Resampler::SampleAndHold);

  // Mix 2 packet queues with the following samples and expected outputs. We'll feed this data
  // through the mix stage in 3 passes of 2ms windows:
  //
  //       -----------------------------------
  // q1   | 0.1 | 0.2 | 0.2 | 0.3 | 0.3 | 0.3 |
  //       -----------------------------------
  // q2   | 0.7 | 0.7 | 0.7 | 0.5 | 0.5 | 0.3 |
  //       -----------------------------------
  // mix  | 0.8 | 0.9 | 0.9 | 0.8 | 0.8 | 0.6 |
  //       -----------------------------------
  // pass |     1     |     2     |     3     |
  //       -----------------------------------

  {
    packet_queue1->PushPacket(packet_factory1.CreatePacket(0.1, zx::msec(1)));
    packet_queue1->PushPacket(packet_factory1.CreatePacket(0.2, zx::msec(2)));
    packet_queue1->PushPacket(packet_factory1.CreatePacket(0.3, zx::msec(3)));
  }
  {
    packet_queue2->PushPacket(packet_factory2.CreatePacket(0.7, zx::msec(3)));
    packet_queue2->PushPacket(packet_factory2.CreatePacket(0.5, zx::msec(2)));
    packet_queue2->PushPacket(packet_factory2.CreatePacket(0.3, zx::msec(1)));
  }

  int64_t output_frame_start = 0;
  uint32_t output_frame_count = 96;
  {  // Mix frames 0-2ms. Expect 1 ms of 0.8 values, then 1 ms of 0.9 values.
    auto buf = mix_stage_->ReadLock(Fixed(output_frame_start), output_frame_count);
    // 1ms @ 48000hz == 48 frames. 2ms == 96 (frames).
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->length().Floor(), 96u);
    // Each frame is 2 channels, so 1ms will be 96 samples.
    auto& arr1 = as_array<float, 96>(buf->payload(), 0);
    EXPECT_THAT(arr1, Each(FloatEq(0.8f)))
        << std::setprecision(5) << "[0] " << arr1[0] << ", [1] " << arr1[1] << ", [94] " << arr1[94]
        << ", [95] " << arr1[95];
    auto& arr2 = as_array<float, 96>(buf->payload(), 96);
    EXPECT_THAT(arr2, Each(FloatEq(0.9f)))
        << std::setprecision(5) << "[0] " << arr2[0] << ", [1] " << arr2[1] << ", [94] " << arr2[94]
        << ", [95] " << arr2[95];
  }

  output_frame_start += output_frame_count;
  {  // Mix frames 2-4ms. Expect 1 ms of 0.9 samples, then 1 ms of 0.8 values.
    auto buf = mix_stage_->ReadLock(Fixed(output_frame_start), output_frame_count);
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->length().Floor(), 96u);

    auto& arr1 = as_array<float, 96>(buf->payload(), 0);
    EXPECT_THAT(arr1, Each(FloatEq(0.9f)))
        << std::setprecision(5) << "[0] " << arr1[0] << ", [1] " << arr1[1] << ", [94] " << arr1[94]
        << ", [95] " << arr1[95];
    auto& arr2 = as_array<float, 96>(buf->payload(), 96);
    EXPECT_THAT(arr2, Each(FloatEq(0.8f)))
        << std::setprecision(5) << "[0] " << arr2[0] << ", [1] " << arr2[1] << ", [94] " << arr2[94]
        << ", [95] " << arr2[95];
    ;
  }

  output_frame_start += output_frame_count;
  {  // Mix frames 4-6ms. Expect 1 ms of 0.8 values, then 1 ms of 0.6 values.
    auto buf = mix_stage_->ReadLock(Fixed(output_frame_start), output_frame_count);
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->length().Floor(), 96u);

    auto& arr1 = as_array<float, 96>(buf->payload(), 0);
    EXPECT_THAT(arr1, Each(FloatEq(0.8f)))
        << std::setprecision(5) << "[0] " << arr1[0] << ", [1] " << arr1[1] << ", [94] " << arr1[94]
        << ", [95] " << arr1[95];
    auto& arr2 = as_array<float, 96>(buf->payload(), 96);
    EXPECT_THAT(arr2, Each(FloatEq(0.6f)))
        << std::setprecision(5) << "[0] " << arr2[0] << ", [1] " << arr2[1] << ", [94] " << arr2[94]
        << ", [95] " << arr2[95];
  }

  // Upon any fail, slab_allocator asserts at exit. Clear all allocations, so testing can continue.
  mix_stage_->Trim(Fixed::Max());
}

TEST_F(MixStageTest, MixUniformFormats) { TestMixStageUniformFormats(ClockMode::SAME); }
TEST_F(MixStageTest, MixUniformFormats_ClockOffset) {
  TestMixStageUniformFormats(ClockMode::WITH_OFFSET);
}

// Validate that a mixer with significant filter width can pull from a source buffer in pieces
// (assuming there is sufficient additional read-ahead data to satisfy the filter width!).
TEST_F(MixStageTest, MixFromRingBuffersSinc) {
  // Note: there are non-obvious constraints on the size of this ring because of how we test below.
  // In ReadLock we specify both a number of frames AND a source reference time to not read beyond.
  // We specify to read at most 1 msec of source, while specifying a number-of-frames well less than
  // that. However, filter width is included in these calculations, which means that:
  // *** Half of the ring duration, PLUS the mixer filter width, must not exceed 1 msec of source.
  // Currently SincSampler's positive_width is 13 frames so (at 48k) our ring must be <= 70 frames.
  // This test should be adjusted if SincSampler's filter width increases.
  constexpr uint32_t kRingSizeFrames = 64;
  constexpr uint32_t kRingSizeSamples = kRingSizeFrames * kDefaultNumChannels;
  constexpr uint32_t kFramesPerMs = 48;

  // Create a new RingBuffer and add it to our mix stage.
  int64_t safe_write_frame = 0;
  auto ring_buffer_endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
      kDefaultFormat, timeline_function_, *device_clock_, kRingSizeFrames,
      [&safe_write_frame] { return safe_write_frame; });

  // We explictly request a SincSampler here to get a non-trivial filter width.
  mix_stage_->AddInput(ring_buffer_endpoints.reader, std::nullopt, Mixer::Resampler::WindowedSinc);

  // Fill up the ring buffer with non-empty samples so we can observe them in the mix output.
  // The first half of the ring is one value, the second half is another.
  constexpr float kRingBufferSampleValue1 = 0.5;
  constexpr float kRingBufferSampleValue2 = 0.7;
  float* ring_buffer_samples = reinterpret_cast<float*>(ring_buffer_endpoints.writer->virt());
  for (size_t sample = 0; sample < kRingSizeSamples / 2; ++sample) {
    ring_buffer_samples[sample] = kRingBufferSampleValue1;
    ring_buffer_samples[kRingSizeSamples / 2 + sample] = kRingBufferSampleValue2;
  }

  // Read the ring in two halves, each is assigned a different source value in the ring above.
  constexpr uint32_t kRequestedFrames = kRingSizeFrames / 2;
  {
    safe_write_frame = 1 * kFramesPerMs;
    auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->start().Floor(), 0u);
    ASSERT_EQ(buf->length().Floor(), kRequestedFrames);

    auto& arr = as_array<float, kRequestedFrames * kDefaultNumChannels>(buf->payload(), 0);
    EXPECT_THAT(arr, Each(FloatEq(kRingBufferSampleValue1)))
        << std::setprecision(5) << "[0] " << arr[0] << ", [" << (arr.size() - 1) << "] "
        << arr[arr.size() - 1];
  }

  {
    safe_write_frame = 2 * kFramesPerMs;
    auto buf = mix_stage_->ReadLock(Fixed(kRequestedFrames), kRequestedFrames);
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->start().Floor(), kRequestedFrames);
    ASSERT_EQ(buf->length().Floor(), kRequestedFrames);

    auto& arr = as_array<float, kRequestedFrames * kDefaultNumChannels>(buf->payload(), 0);
    EXPECT_THAT(arr, Each(FloatEq(kRingBufferSampleValue2)))
        << std::setprecision(5) << "[0] " << arr[0] << ", [" << (arr.size() - 1) << "] "
        << arr[arr.size() - 1];
  }
}

TEST_F(MixStageTest, MixNoInputs) {
  constexpr uint32_t kRequestedFrames = 48;
  auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);

  // With no inputs, we should return nullopt.
  ASSERT_FALSE(buf);
}

TEST_F(MixStageTest, MixSilentInput) {
  // Add a silent input.
  auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
  stream->set_usage_mask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  stream->set_gain_db(fuchsia::media::audio::MUTED_GAIN_DB);
  // Set timeline rate to match our format.
  stream->timeline_function()->Update(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  mix_stage_->AddInput(stream);

  constexpr uint32_t kRequestedFrames = 48;
  auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);

  // If an input is silent, we can return silence.
  ASSERT_FALSE(buf);
}

TEST_F(MixStageTest, MixSilentInputWithNonSilentInput) {
  // Add a silent input.
  auto silent_stream =
      std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
  silent_stream->set_usage_mask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  silent_stream->set_gain_db(fuchsia::media::audio::MUTED_GAIN_DB);
  // Set timeline rate to match our format.
  silent_stream->timeline_function()->Update(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  mix_stage_->AddInput(silent_stream);

  // Add a non-silent input.
  auto non_silent_stream =
      std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
  non_silent_stream->set_usage_mask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  non_silent_stream->set_gain_db(0.0);
  // Set timeline rate to match our format.
  non_silent_stream->timeline_function()->Update(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  mix_stage_->AddInput(non_silent_stream);

  constexpr uint32_t kRequestedFrames = 48;
  auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);

  // If an input is silent, we can return silence.
  ASSERT_TRUE(buf);
}

static constexpr auto kInputStreamUsage = StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION);
void MixStageTest::TestMixStageSingleInput(ClockMode clock_mode) {
  // Set timeline rate to match our format.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  std::shared_ptr<PacketQueue> packet_queue;

  if (clock_mode == ClockMode::SAME) {
    packet_queue = std::make_shared<PacketQueue>(
        kDefaultFormat, timeline_function,
        context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));
  } else if (clock_mode == ClockMode::WITH_OFFSET) {
    auto custom_audio_clock = SetPacketFactoryWithOffsetAudioClock(zx::sec(5), packet_factory);

    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function,
                                                 std::move(custom_audio_clock));
  } else {
    ASSERT_TRUE(clock_mode == ClockMode::RATE_ADJUST) << "Unknown clock mode";
    ASSERT_TRUE(false) << "Multi-rate testing not yet implemented";
  }

  packet_queue->set_usage(kInputStreamUsage);
  mix_stage_->AddInput(packet_queue);

  packet_queue->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(5)));

  constexpr uint32_t kRequestedFrames = 48;
  auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);
  ASSERT_TRUE(buf);
  EXPECT_TRUE(buf->usage_mask().contains(kInputStreamUsage));
  EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), Gain::kUnityGainDb);

  // Upon any fail, slab_allocator asserts at exit. Clear all allocations, so testing can continue.
  mix_stage_->Trim(Fixed::Max());
  mix_stage_->RemoveInput(*packet_queue);
}

TEST_F(MixStageTest, MixSingleInput) { TestMixStageSingleInput(ClockMode::SAME); }
TEST_F(MixStageTest, MixSingleInput_ClockOffset) {
  TestMixStageSingleInput(ClockMode::WITH_OFFSET);
}

TEST_F(MixStageTest, MixMultipleInputs) {
  // Set timeline rate to match our format.
  auto timeline_function = TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto input1 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input1->timeline_function()->Update(timeline_function);
  auto input2 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input2->timeline_function()->Update(timeline_function);
  mix_stage_->AddInput(input1);
  mix_stage_->AddInput(input2);

  constexpr uint32_t kRequestedFrames = 48;

  // The buffer should return the union of the usage mask, and the largest of the input gains.
  input1->set_usage_mask(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)}));
  input1->set_gain_db(-160);
  input2->set_usage_mask(
      StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
  input2->set_gain_db(-15);
  {
    auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(), StreamUsageMask({
                                     StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION),
                                 }));
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), -15);
  }
}

// When mixing streams, a buffer's total_applied_gain_db is set, based on the largest of its
// inputs. Each input's total_applied_gain_db is determined by ITS input's total_applied_gain_db,
// plus its dest_gain.
//
// Validate that source_gain is appropriately incorporated and the correct (max) value is returned.
TEST_F(MixStageTest, BufferGainDbIncludesSourceGain) {
  // Set timeline rate to match our format.
  auto timeline_function = TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto input1 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input1->timeline_function()->Update(timeline_function);
  auto input2 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input2->timeline_function()->Update(timeline_function);
  auto mixer1 = mix_stage_->AddInput(input1);
  auto mixer2 = mix_stage_->AddInput(input2);

  constexpr uint32_t kRequestedFrames = 48;

  // The buffer should return the union of the usage mask, and the largest of the input gains.
  input1->set_usage_mask(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)}));
  input1->set_gain_db(1.0);
  mixer1->bookkeeping().gain.SetSourceGain(-160);
  input2->set_usage_mask(
      StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
  input2->set_gain_db(0.0);
  mixer2->bookkeeping().gain.SetSourceGain(-15);
  {
    auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(), StreamUsageMask({
                                     StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION),
                                 }));
    // If the source gain is included in the calculation, then input2 should be the larger value.
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), -15.0);
  }
}

// Validate that dest_gain is appropriately incorporated and the correct (max) value is returned.
TEST_F(MixStageTest, BufferMaxAmplitudeIncludesDestGain) {
  // Set timeline rate to match our format.
  auto timeline_function = TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto input1 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input1->timeline_function()->Update(timeline_function);
  auto input2 = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                      zx_system_get_page_size());
  input2->timeline_function()->Update(timeline_function);
  auto mixer1 = mix_stage_->AddInput(input1);
  auto mixer2 = mix_stage_->AddInput(input2);

  constexpr uint32_t kRequestedFrames = 48;

  // The buffer should return the union of the usage mask, and the largest of the input gains.
  input1->set_usage_mask(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)}));
  input1->set_gain_db(1.0);
  mixer1->bookkeeping().gain.SetDestGain(-160);
  input2->set_usage_mask(
      StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
  input2->set_gain_db(0.0);
  mixer2->bookkeeping().gain.SetDestGain(-15);
  {
    auto buf = mix_stage_->ReadLock(Fixed(0), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(), StreamUsageMask({
                                     StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION),
                                 }));
    // If destination gain is included in the calculation, then input2 should be the larger value.
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), -15);
  }
}

TEST_F(MixStageTest, CachedUntilFullyConsumed) {
  // Create a packet queue to use as our source stream.
  auto stream = std::make_shared<PacketQueue>(
      kDefaultFormat, timeline_function_,
      context().clock_factory()->CreateClientFixed(clock::CloneOfMonotonic()));

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  bool packet_released = false;
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10),
                                                 [&packet_released] { packet_released = true; }));
  auto mix_stage =
      std::make_shared<MixStage>(kDefaultFormat, 480, timeline_function_, *device_clock_);
  mix_stage->AddInput(stream);

  // After mixing half the packet, the packet should not be released.
  {
    auto buf = mix_stage->ReadLock(Fixed(0), 240);
    RunLoopUntilIdle();
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(240u, buf->length().Floor());
    EXPECT_EQ(1.0, static_cast<float*>(buf->payload())[0]);
    EXPECT_FALSE(packet_released);
  }

  RunLoopUntilIdle();
  EXPECT_FALSE(packet_released);

  // After mixing all of the packet, the packet should be released.
  // However, we set fully consumed = false so the mix buffer will be cached.
  {
    auto buf = mix_stage->ReadLock(Fixed(0), 480);
    RunLoopUntilIdle();
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(480u, buf->length().Floor());
    EXPECT_EQ(1.0, static_cast<float*>(buf->payload())[0]);
    EXPECT_TRUE(packet_released);
    buf->set_is_fully_consumed(false);
  }

  // Mixing again should return the same buffer.
  // This time we set fully consumed = true to discard the cached mix result.
  {
    auto buf = mix_stage->ReadLock(Fixed(0), 480);
    RunLoopUntilIdle();
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(480u, buf->length().Floor());
    EXPECT_EQ(1.0, static_cast<float*>(buf->payload())[0]);
    buf->set_is_fully_consumed(true);
  }

  // The mix buffer is not cached and the packet is gone, so we must mix silence.
  {
    auto buf = mix_stage->ReadLock(Fixed(0), 480);
    RunLoopUntilIdle();
    ASSERT_FALSE(buf);
  }
}

// Double-check the reset of rate-adjustment coefficients upon first ReadLock call, and validate
// that source_pos_modulo is not being double-incremented.
TEST_F(MixStageTest, PositionResetAndAdvance) {
  constexpr int32_t dest_frames_per_mix = 96;

  // We set our timeline slow by 1 source_pos_modulo unit per frame.
  auto nsec_to_frac_source =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(kDefaultFormat.frames_per_second()).raw_value() - 1, zx::sec(1).to_nsecs())));
  // Set PacketQueue with a clone of the device clock, so micro-SRC doesn't engage.
  std::shared_ptr<PacketQueue> packet_queue = std::make_shared<PacketQueue>(
      kDefaultFormat, nsec_to_frac_source, std::move(clone_of_device_clock_));

  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  bool packet_released = false;
  packet_queue->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(2)));
  packet_queue->PushPacket(packet_factory.CreatePacket(2.0, zx::msec(2)));
  packet_queue->PushPacket(packet_factory.CreatePacket(
      3.0, zx::msec(2), [&packet_released] { packet_released = true; }));

  auto mixer = mix_stage_->AddInput(packet_queue, 0.0f, Mixer::Resampler::WindowedSinc);
  auto& info = mixer->source_info();
  auto& bookkeeping = mixer->bookkeeping();

  bookkeeping.SetRateModuloAndDenominator(76543, 98765);
  bookkeeping.source_pos_modulo = 23456;

  auto source_pos_for_read_lock = Fixed(0);
  // The first mix resets position, so the above will be overwritten and we'll advance from zero.
  {
    auto buffer = mix_stage_->ReadLock(source_pos_for_read_lock, dest_frames_per_mix);
    RunLoopUntilIdle();

    ASSERT_TRUE(buffer);
    EXPECT_EQ(source_pos_for_read_lock.Floor(), buffer->start().Floor());
    EXPECT_EQ(dest_frames_per_mix, buffer->length().Floor());
    source_pos_for_read_lock += Fixed(dest_frames_per_mix);

    // At a 48k nominal rate, we expect rate_modulo to be 47999 and denom to be 48000.
    EXPECT_EQ(bookkeeping.step_size, Fixed(kOneFrame - Fixed::FromRaw(1)))
        << ffl::String::DecRational << bookkeeping.step_size;
    EXPECT_EQ(bookkeeping.rate_modulo(),
              static_cast<uint64_t>(kDefaultFormat.frames_per_second()) - 1);
    EXPECT_EQ(bookkeeping.denominator(), static_cast<uint64_t>(kDefaultFormat.frames_per_second()));

    // source_pos_modulo should show that we lose 1 source_pos_modulo per dest frame.
    EXPECT_EQ(bookkeeping.source_pos_modulo,
              bookkeeping.denominator() - source_pos_for_read_lock.Floor());
    // ... which also means we'll be one frac-frame behind.
    EXPECT_EQ(info.next_source_frame, Fixed(Fixed(info.next_dest_frame) - Fixed::FromRaw(1)))
        << ffl::String::DecRational << info.next_source_frame;
  }

  {
    auto buffer = mix_stage_->ReadLock(source_pos_for_read_lock, dest_frames_per_mix);
    RunLoopUntilIdle();

    ASSERT_TRUE(buffer);
    EXPECT_EQ(source_pos_for_read_lock.Floor(), buffer->start().Floor());
    EXPECT_EQ(dest_frames_per_mix, buffer->length().Floor());
    source_pos_for_read_lock += Fixed(dest_frames_per_mix);

    EXPECT_EQ(bookkeeping.step_size, Fixed(kOneFrame - Fixed::FromRaw(1)))
        << ffl::String::DecRational << bookkeeping.step_size;
    EXPECT_EQ(bookkeeping.rate_modulo(),
              static_cast<uint64_t>(kDefaultFormat.frames_per_second()) - 1);
    EXPECT_EQ(bookkeeping.denominator(), static_cast<uint64_t>(kDefaultFormat.frames_per_second()));

    EXPECT_EQ(bookkeeping.source_pos_modulo,
              bookkeeping.denominator() - source_pos_for_read_lock.Floor());
    EXPECT_EQ(info.next_source_frame, Fixed(Fixed(info.next_dest_frame) - Fixed::FromRaw(1)))
        << ffl::String::DecRational << info.next_source_frame;
  }

  // Subsequent mixes should not reset position, so this change should persist.
  bookkeeping.source_pos_modulo += 17;
  {
    auto buffer = mix_stage_->ReadLock(Fixed(source_pos_for_read_lock), dest_frames_per_mix);
    RunLoopUntilIdle();

    ASSERT_TRUE(buffer);
    EXPECT_EQ(source_pos_for_read_lock.Floor(), buffer->start().Floor());
    EXPECT_EQ(dest_frames_per_mix, buffer->length().Floor());
    source_pos_for_read_lock += Fixed(dest_frames_per_mix);

    EXPECT_EQ(bookkeeping.step_size, Fixed(kOneFrame - Fixed::FromRaw(1)))
        << ffl::String::DecRational << bookkeeping.step_size;
    EXPECT_EQ(bookkeeping.rate_modulo(),
              static_cast<uint64_t>(kDefaultFormat.frames_per_second()) - 1);
    EXPECT_EQ(bookkeeping.denominator(), static_cast<uint64_t>(kDefaultFormat.frames_per_second()));

    // source_pos_modulo shows the offset, and still losing 1 source_pos_modulo per dest frame
    EXPECT_EQ(bookkeeping.source_pos_modulo,
              bookkeeping.denominator() - source_pos_for_read_lock.Floor() + 17);
    EXPECT_EQ(info.next_source_frame, Fixed(Fixed(info.next_dest_frame) - Fixed::FromRaw(1)))
        << ffl::String::DecRational << info.next_source_frame;
  }

  packet_queue->Flush();
  while (!packet_released) {
    RunLoopUntilIdle();
  }
}

// This is a regression test for fxbug.dev/67996.
TEST_F(MixStageTest, DontCrashOnDestOffsetRoundingError) {
  // Unused, but MixStage::ProcessMix needs this argument.
  auto input = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory(),
                                                     zx_system_get_page_size());

  // As summarized in the calculations at the link below, the following hard-coded source_info
  // values result in dest_offset = 301. In order for this offset to not overflow the dest buffer,
  // we need at least 302 frames in the MixStage output buffer.
  // https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=67996#c22
  //
  // We use 480, which is 10ms at 48kHz.
  mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, 480 /* block size in frames */,
                                          timeline_function_, *device_clock_);

  // The crash happened in PointSampler.
  auto mixer = mix_stage_->AddInput(input, std::nullopt, Mixer::Resampler::SampleAndHold);

  // First step of ReadLock.
  memset(&mix_stage_->cur_mix_job_, 0, sizeof(mix_stage_->cur_mix_job_));

  // The following values are derived from an actual crash. We set only the values needed by
  // MixStage::ProcessMix. The crux of the bug is that the dest clock's adjusted rate of -1 PPM
  // caused a rounding error. See discussion at fxbug.dev/67996#c22.
  mix_stage_->cur_mix_job_.buf = &mix_stage_->output_buffer_[0];
  mix_stage_->cur_mix_job_.buf_frames = mix_stage_->output_buffer_frames_;
  mix_stage_->cur_mix_job_.dest_ref_clock_to_frac_dest_frame = TimelineFunction();
  mixer->source_info().frames_produced = 0;
  mixer->source_info().dest_frames_to_frac_source_frames =
      TimelineFunction(3582737759, 0, 8192000, 999);
  mixer->source_info().next_source_frame = Fixed::FromRaw(2414202275419);
  mixer->bookkeeping().step_size = Fixed(1);
  mixer->bookkeeping().SetRateModuloAndDenominator(0, 1);

  char payload[10 * kDefaultNumChannels * sizeof(float)];
  ReadableStream::Buffer buffer(Fixed::FromRaw(2414204747776), Fixed(10), payload, true,
                                StreamUsageMask(), 0);

  mix_stage_->ProcessMix(*mixer, *input, buffer);
}

// When a packet starts after the mix starts, position should be advanced per step_size|rate_mod,
// including updating source_pos_modulo (not simply scaled with a TimelineRate).
TEST_F(MixStageTest, PositionSkip) {
  constexpr int32_t dest_frames_per_mix = 48;  // 1ms

  // We set our timeline slow by 1 frac-frame per msec, to create source_pos_modulo activity.
  auto nsec_to_frac_source =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(kDefaultFormat.frames_per_second()).raw_value() - 1, zx::sec(1).to_nsecs())));
  std::shared_ptr<PacketQueue> packet_queue = std::make_shared<PacketQueue>(
      kDefaultFormat, nsec_to_frac_source, std::move(clone_of_device_clock_));

  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  bool packet_released = false;
  packet_queue->PushPacket(packet_factory.CreatePacket(
      1.0, zx::msec(1), [&packet_released] { packet_released = true; }));

  auto mixer = mix_stage_->AddInput(packet_queue, 0.0f, Mixer::Resampler::WindowedSinc);

  auto source_pos_for_read_lock = Fixed(-mixer->pos_filter_width() + Fixed::FromRaw(4000));
  // The first mix resets position, so the above will be overwritten and we'll advance from zero.
  {
    auto buffer = mix_stage_->ReadLock(source_pos_for_read_lock, dest_frames_per_mix);
    RunLoopUntilIdle();

    ASSERT_TRUE(buffer);
    EXPECT_EQ(source_pos_for_read_lock.Floor(), buffer->start().Floor());
    EXPECT_EQ(dest_frames_per_mix, buffer->length().Floor());
    source_pos_for_read_lock += Fixed(dest_frames_per_mix);

    // At a 48k nominal rate, we expect rate_modulo to be 47999 and denom to be 48000.
    // source_pos_modulo should show that we lose 1 source_pos_modulo per dest frame.
    // ... which also means our running source position will be 1 frac-frame behind.
    auto& bookkeeping = mixer->bookkeeping();
    EXPECT_EQ(bookkeeping.step_size, Fixed(kOneFrame - Fixed::FromRaw(1)))
        << ffl::String::DecRational << bookkeeping.step_size;
    EXPECT_EQ(bookkeeping.rate_modulo(),
              static_cast<uint64_t>(kDefaultFormat.frames_per_second()) - 1);
    EXPECT_EQ(bookkeeping.denominator(), static_cast<uint64_t>(kDefaultFormat.frames_per_second()));

    auto& info = mixer->source_info();
    EXPECT_EQ(info.frames_produced, dest_frames_per_mix);
    EXPECT_EQ(info.next_dest_frame, source_pos_for_read_lock.Floor());
    EXPECT_EQ(info.next_source_frame, Fixed(Fixed(info.next_dest_frame) - Fixed::FromRaw(1)))
        << ffl::String::DecRational << info.next_source_frame;

    EXPECT_EQ(bookkeeping.source_pos_modulo, bookkeeping.denominator() - dest_frames_per_mix);
  }

  packet_queue->Flush();
  while (!packet_released) {
    RunLoopUntilIdle();
  }
}

}  // namespace media::audio
