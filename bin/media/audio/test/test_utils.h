// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#pragma once

#include "apps/media/src/audio/mixdown_table.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// A function that produces samples as a function of channel and pts.
template <typename TSample>
using SampleFunc = std::function<TSample(uint32_t, int64_t)>;

// SampleFunc that produces silent samples.
template <typename TSample>
TSample SilentSamples(uint32_t channel, int64_t pts);

// SampleFunc that produces silent samples, float specialization.
template <>
float SilentSamples(uint32_t channel, int64_t pts);

// SampleFunc that produces distinct samples.
template <typename TSample>
TSample DistinctSamples(uint32_t channel, int64_t pts);

// SampleFunc that produces distinct samples, float specialization.
template <>
float DistinctSamples(uint32_t channel, int64_t pts);

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
                              float to_sample);

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
                const SampleFunc<TSample>& sample_func) {
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
                  const SampleFunc<TSample>& sample_func) {
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      EXPECT_EQ(sample_func(channel, first_pts + frame_index),
                buffer[channel_count * frame_index + channel]);
    }
  }
}

// Determines if two floats are equal within a tolerance of +/- |epsilon|.
bool RoughlyEquals(float a, float b, float epsilon = 0.0001f);

// Verifies a buffer using the provided SampleFunc, float specialization.
template <>
void VerifyBuffer(float* buffer,
                  uint32_t channel_count,
                  uint32_t frame_count,
                  int64_t first_pts,
                  const SampleFunc<float>& sample_func);

}  // namespace test
}  // namespace media
