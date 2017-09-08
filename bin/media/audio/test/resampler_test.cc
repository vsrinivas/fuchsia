// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/resampler.h"

#include <vector>

#include "garnet/bin/media/audio/test/test_utils.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Runs a resampler test in which an input buffer and an output buffer
// are exhausted in multiple |Resample| calls.
template <typename TSample, typename TSubframeIndex>
void ChunkyResamplerTest(Resampler<TSample, TSubframeIndex>* under_test,
                         const std::vector<size_t>& in_buf_chunks,
                         const SampleFunc<TSample>& in_sample_func,
                         const std::vector<size_t>& out_buf_chunks,
                         const SampleFunc<TSample>& out_sample_func) {
  uint32_t channel_count = under_test->channel_count();

  size_t in_buf_size = 0;
  for (size_t buf_chunk : in_buf_chunks) {
    in_buf_size += buf_chunk;
  }

  size_t out_buf_size = 0;
  for (size_t buf_chunk : out_buf_chunks) {
    out_buf_size += buf_chunk;
  }

  std::vector<TSample> in_buffer(channel_count * in_buf_size);
  std::vector<TSample> out_buffer(channel_count * out_buf_size);

  FillBuffer<TSample>(in_buffer.data(), channel_count,
                      in_buffer.size() / channel_count, 0, in_sample_func);

  size_t in_buf_chunk_index = 0;
  size_t out_buf_chunk_index = 0;
  size_t in_buf_index = 0;
  size_t out_buf_index = 0;

  while (in_buf_index < in_buffer.size() || out_buf_index < out_buffer.size()) {
    if (under_test->need_in_frames()) {
      EXPECT_LT(in_buf_chunk_index, in_buf_chunks.size());
      EXPECT_LT(in_buf_index, in_buffer.size());
      under_test->PutInFrames(in_buffer.data() + in_buf_index,
                              in_buf_chunks[in_buf_chunk_index]);
      in_buf_index += in_buf_chunks[in_buf_chunk_index] * channel_count;
      ++in_buf_chunk_index;
    }

    if (under_test->need_out_frames()) {
      EXPECT_LT(out_buf_chunk_index, out_buf_chunks.size());
      EXPECT_LT(out_buf_index, out_buffer.size());
      under_test->PutOutFrames(out_buffer.data() + out_buf_index,
                               out_buf_chunks[out_buf_chunk_index]);
      out_buf_index += out_buf_chunks[out_buf_chunk_index] * channel_count;
      ++out_buf_chunk_index;
    }

    under_test->Resample();
  }

  EXPECT_EQ(in_buf_index, in_buffer.size());
  EXPECT_EQ(out_buf_index, out_buffer.size());

  EXPECT_TRUE(under_test->need_in_frames());
  EXPECT_TRUE(under_test->need_out_frames());

  VerifyBuffer<TSample>(out_buffer.data(), channel_count,
                        out_buffer.size() / channel_count, 0, out_sample_func);
}

// Tests that a new resampler behaves as expected.
TEST(ResamplerTest, InitialState) {
  Resampler<float, float> under_test(2);
  EXPECT_EQ(2u, under_test.channel_count());

  EXPECT_EQ(nullptr, under_test.in_frames());
  EXPECT_EQ(0u, under_test.in_frames_remaining());
  EXPECT_TRUE(under_test.need_in_frames());

  EXPECT_EQ(nullptr, under_test.out_frames());
  EXPECT_EQ(0u, under_test.out_frames_remaining());
  EXPECT_TRUE(under_test.need_out_frames());

  EXPECT_EQ(0, under_test.pts());
}

// Verifies pass-through operation.
TEST(ResamplerTest, Passthrough) {
  Resampler<float, float> under_test(2);
  ChunkyResamplerTest<float, float>(&under_test, {256}, DistinctSamples<float>,
                                    {256}, DistinctSamples<float>);
}

// Verifies chunky pass-through operation.
TEST(ResamplerTest, ChunkyPassthrough) {
  Resampler<float, float> under_test(2);
  ChunkyResamplerTest<float, float>(
      &under_test, {1, 8, 12, 100, 64, 71}, DistinctSamples<float>,
      {100, 71, 12, 1, 64, 8}, DistinctSamples<float>);
}

// Interpolates many output frames from two input frames.
TEST(ResamplerTest, BetweenTwoFrames) {
  Resampler<float, float> under_test(2);
  under_test.AddRateScheduleEntry(TimelineRate(1, 100), 0);
  ChunkyResamplerTest<float, float>(
      &under_test, {2}, FadeSamples<float>(0, 0.0f, 1, 100.0f), {101},
      FadeSamples<float>(0, 0.0f, 100, 100.0f));
}

// Copies two output frames from many input frames.
TEST(ResamplerTest, FlyoverFrames) {
  Resampler<float, float> under_test(2);
  under_test.AddRateScheduleEntry(TimelineRate(100, 1), 0);
  ChunkyResamplerTest<float, float>(
      &under_test, {101}, FadeSamples<float>(0, 0.0f, 100, 100.0f), {2},
      FadeSamples<float>(0, 0.0f, 1, 100.0f));
}

}  // namespace test
}  // namespace media
