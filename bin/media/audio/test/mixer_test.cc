// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/mixer.h"

#include <cmath>

#include "apps/media/lib/timeline_rate.h"
#include "apps/media/src/audio/mixer_input_impl.h"
#include "gtest/gtest.h"

namespace media {
namespace {

// A function that produces samples as a function of channel and pts.
template <typename TSample>
using SampleFunc = std::function<TSample(uint32_t, int64_t)>;

// SampleFunc that produces silent samples.
template <typename TSample>
TSample SilentSamples(uint32_t channel, int64_t pts);

// SampleFunc that produces silent samples, float specialization.
template <>
float SilentSamples(uint32_t channel, int64_t pts) {
  return 0.0f;
}

// SampleFunc that produces distinct samples.
template <typename TSample>
TSample DistinctSamples(uint32_t channel, int64_t pts);

// SampleFunc that produces distinct samples, float specialization.
template <>
float DistinctSamples(uint32_t channel, int64_t pts) {
  return float(pts) / float(1 + channel);
}

// Creates a SampleFunc that returns a fixed sample value.
template <typename TSample>
SampleFunc<TSample> FixedSamples(TSample sample) {
  return [sample](uint32_t channel, int64_t pts) { return sample; };
}

// Creates a SampleFunc that returns a fade from one sample value to another
// across all channels.
template <typename TSample>
SampleFunc<TSample> FadeSamples(int64_t from_pts,
                                TSample from_sample,
                                int64_t to_pts,
                                TSample to_sample);

// Creates a SampleFunc that returns a fade from one sample value to another
// across all channels, float specialization.
template <>
SampleFunc<float> FadeSamples(int64_t from_pts,
                              float from_sample,
                              int64_t to_pts,
                              float to_sample) {
  return [=](uint32_t channel, int64_t pts) {
    return from_sample +
           (to_sample - from_sample) * (float(pts) - float(from_pts)) /
               (float(to_pts) - float(from_pts));
  };
}

// A SampleFunc that composes other SampleFuncs or sample values to create a
// sequence. Example: SampleSequence<float>().At(0, 1.0f).At(50, 2.0f) is
// silent in the PTS range min..-1, 1.0f in the PTS range 0..49 and 2.0f in
// the PTS range 50..max.
template <typename TSample>
struct SampleSequence {
  SampleSequence& At(int64_t from, const SampleFunc<TSample>& func) {
    sequence_.emplace_back(from, func);
    return *this;
  }

  SampleSequence& At(int64_t from, TSample fixed_value) {
    sequence_.emplace_back(from, FixedSamples<TSample>(fixed_value));
    return *this;
  }

  TSample operator()(uint32_t channel, int64_t pts) {
    int64_t best_pts = std::numeric_limits<int64_t>::min();
    const SampleFunc<TSample>* best_func = nullptr;
    for (const auto& pair : sequence_) {
      if (pair.first <= pts && pair.first > best_pts) {
        best_pts = pair.first;
        best_func = &pair.second;
      }
    }

    return best_func == nullptr ? SilentSamples<TSample>(channel, pts)
                                : (*best_func)(channel, pts);
  }

 private:
  std::vector<std::pair<int64_t, SampleFunc<TSample>>> sequence_;
};

// Creates a MixdownTable that silences all input channels.
template <typename TLevel>
std::unique_ptr<MixdownTable<TLevel>> SilentMix(uint32_t in_channel_count,
                                                uint32_t out_channel_count) {
  return MixdownTable<TLevel>::CreateSilent(in_channel_count,
                                            out_channel_count);
}

// Creates a square MixdownTable passes all input channels to their respective
// output channels at unity.
template <typename TLevel>
std::unique_ptr<MixdownTable<TLevel>> PassthroughMix(uint32_t channel_count) {
  return MixdownTable<TLevel>::CreatePassthrough(channel_count);
}

// Creates a square MixdownTable that adjusts channel levels without
// cross-mixing.
template <typename TLevel>
std::unique_ptr<MixdownTable<TLevel>> LevelMix(uint32_t channel_count,
                                               TLevel level) {
  std::unique_ptr<MixdownTable<TLevel>> table =
      MixdownTable<TLevel>::CreateSilent(channel_count, channel_count);
  for (uint32_t channel = 0; channel < channel_count; ++channel) {
    table->get_level(channel, channel) = Level<TLevel>(level);
  }

  return table;
}

// Creates a MixdownTable mixes all input channels into all output channels at
// unity.
template <typename TLevel>
std::unique_ptr<MixdownTable<TLevel>> UnityMix(uint32_t in_channel_count,
                                               uint32_t out_channel_count) {
  std::unique_ptr<MixdownTable<TLevel>> table =
      MixdownTable<TLevel>::CreateSilent(in_channel_count, out_channel_count);
  for (uint32_t out_channel = 0; out_channel < out_channel_count;
       ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < in_channel_count; ++in_channel) {
      table->get_level(in_channel, out_channel) = Level<TLevel>::Unity;
    }
  }

  return table;
}

// Fills a buffer using the provided SampleFunc.
template <typename TSample>
void FillBuffer(TSample* buffer,
                uint32_t channel_count,
                uint32_t frame_count,
                int64_t first_pts,
                SampleFunc<TSample> sample_func) {
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      buffer[channel_count * frame_index + channel] =
          sample_func(channel, first_pts + frame_index);
    }
  }
}

// Verifies a buffer using the provided SampleFunc.
template <typename TSample>
void VerifyBuffer(TSample* buffer,
                  uint32_t channel_count,
                  uint32_t frame_count,
                  int64_t first_pts,
                  SampleFunc<TSample> sample_func) {
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      EXPECT_EQ(sample_func(channel, first_pts + frame_index),
                buffer[channel_count * frame_index + channel]);
    }
  }
}

// Determines if two floats are equal within a tolerance of +/- 0.000001.
bool RoughlyEquals(float a, float b) {
  static constexpr float kEpsilon = 0.000001f;
  return std::abs(a - b) < kEpsilon;
}

// Verifies a buffer using the provided SampleFunc, float specialization.
template <>
void VerifyBuffer(float* buffer,
                  uint32_t channel_count,
                  uint32_t frame_count,
                  int64_t first_pts,
                  SampleFunc<float> sample_func) {
  bool all_samples_verified = true;
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      if (!RoughlyEquals(sample_func(channel, first_pts + frame_index),
                         buffer[channel_count * frame_index + channel])) {
        FTL_LOG(WARNING) << "VerifyBuffer expected "
                         << sample_func(channel, first_pts + frame_index)
                         << " got "
                         << buffer[channel_count * frame_index + channel]
                         << " on channel " << channel << " at pts "
                         << first_pts + frame_index;
        all_samples_verified = false;
      }
    }
  }

  EXPECT_TRUE(all_samples_verified);
}

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
    FTL_DCHECK(input_index < inputs_.size());
    inputs_[input_index]->AddMixdownScheduleEntry(std::move(table), pts, fade);
  }

  // Adds an input packet, filled using the specified SampleFunc, to the
  // specified input.
  void AddInputPacket(size_t input_index,
                      uint32_t frame_count,
                      int64_t pts,
                      const SampleFunc<TInSample>& sample_func,
                      bool end_of_stream = false) {
    FTL_DCHECK(input_index < inputs_.size());
    MixerInputImpl<TInSample, TOutSample, TLevel>& input =
        *inputs_[input_index];
    size_t size = frame_count * input.in_channel_count() * sizeof(TInSample);
    PacketPtr packet =
        Packet::Create(pts, TimelineRate(48000, 1), end_of_stream, size,
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

}  // namespace
}  // namespace media
