// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/mixer.h"

#include "lib/media/timeline/timeline_rate.h"
#include "garnet/bin/media/audio/mixer_input_impl.h"
#include "garnet/bin/media/audio/test/test_utils.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Mixer test bench.
template <typename TInSample, typename TOutSample, typename TLevel>
class Bench : public PayloadAllocator {
 public:
  Bench(uint32_t out_channel_count)
      : out_channel_count_(out_channel_count), under_test_(out_channel_count) {}

  // Adds an input to the mixer.
  size_t AddInput(uint32_t in_channel_count, int64_t first_pts) {
    std::shared_ptr<MixerInputImpl<TInSample, TOutSample, TLevel>> input =
        MixerInputImpl<TInSample, TOutSample, TLevel>::Create(
            in_channel_count, out_channel_count_, first_pts);
    inputs_.push_back(input);
    under_test_.AddInput(input);
    return inputs_.size() - 1;
  }

  // Adds a schedule entry to the specified input.
  void AddScheduleEntry(size_t input_index,
                        std::unique_ptr<MixdownTable<TLevel>> table,
                        int64_t pts,
                        bool fade) {
    FXL_DCHECK(input_index < inputs_.size());
    inputs_[input_index]->AddMixdownScheduleEntry(std::move(table), pts, fade);
  }

  // Adds an input packet, filled using the specified SampleFunc, to the
  // specified input.
  void AddInputPacket(size_t input_index,
                      uint32_t frame_count,
                      int64_t pts,
                      const SampleFunc<TInSample>& sample_func,
                      bool end_of_stream = false) {
    FXL_DCHECK(input_index < inputs_.size());
    MixerInputImpl<TInSample, TOutSample, TLevel>& input =
        *inputs_[input_index];
    size_t size = frame_count * input.in_channel_count() * sizeof(TInSample);
    PacketPtr packet =
        Packet::Create(pts, TimelineRate(48000, 1), false, end_of_stream, size,
                       AllocatePayloadBuffer(size), this);
    FillBuffer(static_cast<TInSample*>(packet->payload()),
               input.in_channel_count(), frame_count, pts, sample_func);
    input.SupplyPacket(std::move(packet));
  }

  // Mixes to an output buffer, prefilling and verifying the buffer using the
  // specified SampleFuncs.
  void Mix(
      uint32_t frame_count,
      int64_t pts,
      const SampleFunc<TInSample>& verify_sample_func,
      const SampleFunc<TInSample>& prefill_sample_func = SilentSamples<float>) {
    std::unique_ptr<TOutSample[]> out_buffer(
        new TOutSample[frame_count * out_channel_count_]);
    FillBuffer(out_buffer.get(), out_channel_count_, frame_count, pts,
               prefill_sample_func);
    under_test_.Mix(out_buffer.get(), frame_count, pts);
    VerifyBuffer(out_buffer.get(), out_channel_count_, frame_count, pts,
                 verify_sample_func);
  }

 private:
  // PayloadAllocator implementation.
  void* AllocatePayloadBuffer(size_t size) override {
    return std::malloc(size);
  }

  void ReleasePayloadBuffer(void* buffer) override { std::free(buffer); }

  uint32_t out_channel_count_;
  Mixer<TOutSample> under_test_;
  std::vector<std::shared_ptr<MixerInputImpl<TInSample, TOutSample, TLevel>>>
      inputs_;
};

// Tests that the mixer leaves the output buffer unchanged if there are no
// inputs.
TEST(MixerTest, NoInputs) {
  Bench<float, float, float> bench(2);
  bench.Mix(100, 0, DistinctSamples<float>, DistinctSamples<float>);
}

// Tests that the mixer leaves the output buffer unchanged if there is one input
// with no packets and a default mixtable schedule.
TEST(MixerTest, OneInputWithNoPacketsDefaultSchedule) {
  Bench<float, float, float> bench(2);
  bench.AddInput(5, 0);
  bench.Mix(100, 0, DistinctSamples<float>, DistinctSamples<float>);
}

// Tests that the mixer leaves the output buffer unchanged if there is one input
// with a packet and a default mixtable schedule.
TEST(MixerTest, OneInputWithPacketDefaultSchedule) {
  Bench<float, float, float> bench(2);
  bench.AddInput(5, 0);
  bench.AddInputPacket(0, 100, 0, DistinctSamples<float>);
  bench.Mix(100, 0, DistinctSamples<float>, DistinctSamples<float>);
}

// Tests that the mixer leaves the output buffer unchanged if there is one input
// with no packets and a non-silent mixtable schedule.
TEST(MixerTest, OneInputWithNoPacketsNonsilentSchedule) {
  Bench<float, float, float> bench(2);
  bench.AddInput(5, 0);
  bench.AddScheduleEntry(0, UnityMix<float>(5, 2), 0, false);
  bench.Mix(100, 0, DistinctSamples<float>, DistinctSamples<float>);
}

// Tests that the mixer modifies the output buffer properly if there is one
// input with a packet and a passthrough mixtable schedule.
TEST(MixerTest, OneInputWithPacketsPassthroughSchedule) {
  Bench<float, float, float> bench(3);
  bench.AddInput(3, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(3), 0, false);
  bench.AddInputPacket(0, 100, 0, DistinctSamples<float>);
  bench.Mix(100, 0, DistinctSamples<float>);
}

// Tests the concatenation of two input buffers into a single output buffer.
TEST(MixerTest, ConcatenateTwoInputBuffers) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 0, false);
  bench.AddInputPacket(0, 50, 0, FixedSamples<float>(1.0f));
  bench.AddInputPacket(0, 50, 50, FixedSamples<float>(2.0f));
  bench.Mix(100, 0, SampleSequence<float>().At(0, 1.0f).At(50, 2.0f));
}

// Tests the division of one input buffer into two output buffers.
TEST(MixerTest, ConcatenateTwoOutputBuffers) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 0, false);
  bench.AddInputPacket(0, 100, 0, FixedSamples<float>(1.0f));
  bench.Mix(50, 0, FixedSamples<float>(1.0f));
  bench.Mix(50, 50, FixedSamples<float>(1.0f));
}

// Tests a single input buffer that starts before the output buffer.
TEST(MixerTest, EarlyInputBuffer) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 0, false);
  bench.AddInputPacket(0, 100, -50, FixedSamples<float>(1.0f));
  bench.Mix(100, 0, SampleSequence<float>().At(0, 1.0f).At(50, 0.0f));
}

// Tests a single input buffer that starts after the output buffer starts.
TEST(MixerTest, LateInputBuffer) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 0, false);
  bench.AddInputPacket(0, 100, 50, FixedSamples<float>(1.0f));
  bench.Mix(100, 0, SampleSequence<float>().At(0, 0.0f).At(50, 1.0f));
}

// Tests that the mixer input ignores mixdown schedule entries that are
// scheduled before the output buffer PTS.
TEST(MixerTest, ExpiredMixScheduleEntry) {
  Bench<float, float, float> bench(3);
  bench.AddInput(3, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(3), 0, false);
  bench.AddScheduleEntry(0, SilentMix<float>(3, 3), -100, false);
  bench.AddInputPacket(0, 100, 0, DistinctSamples<float>);
  bench.Mix(100, 0, DistinctSamples<float>);
}

// Tests that the mixer input handles a level change in the middle of aligned
// input and output buffers.
TEST(MixerTest, TwoMixScheduleEntries) {
  Bench<float, float, float> bench(3);
  bench.AddInput(3, 0);
  bench.AddScheduleEntry(0, PassthroughMix<float>(3), 0, false);
  bench.AddScheduleEntry(0, LevelMix(3, 2.0f), 50, false);
  bench.AddInputPacket(0, 100, 0, FixedSamples<float>(1.0f));
  bench.Mix(100, 0, SampleSequence<float>().At(0, 1.0f).At(50, 2.0f));
}

// Tests that the mixer input handles fade across aligned input and output
// buffers.
TEST(MixerTest, Fade) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, SilentMix<float>(1, 1), 0, false);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 100, true);
  bench.AddInputPacket(0, 100, 0, FixedSamples<float>(1.0f));
  bench.Mix(100, 0, FadeSamples<float>(0, 0.0f, 100, 1.0f));
}

// Tests that the mixer input handles fade across two input buffers.
TEST(MixerTest, FadeTwoInputBuffers) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, SilentMix<float>(1, 1), 0, false);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 100, true);
  bench.AddInputPacket(0, 50, 0, FixedSamples<float>(1.0f));
  bench.AddInputPacket(0, 50, 50, FixedSamples<float>(1.0f));
  bench.Mix(100, 0, FadeSamples<float>(0, 0.0f, 100, 1.0f));
}

// Tests that the mixer input handles fade across two output buffers.
TEST(MixerTest, FadeTwoOutputBuffers) {
  Bench<float, float, float> bench(1);
  bench.AddInput(1, 0);
  bench.AddScheduleEntry(0, SilentMix<float>(1, 1), 0, false);
  bench.AddScheduleEntry(0, PassthroughMix<float>(1), 100, true);
  bench.AddInputPacket(0, 100, 0, FixedSamples<float>(1.0f));
  bench.Mix(50, 0, FadeSamples<float>(0, 0.0f, 100, 1.0f));
  bench.Mix(50, 50, FadeSamples<float>(0, 0.0f, 100, 1.0f));
}

}  // namespace test
}  // namespace media
