// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_BYTE_RATE_ESTIMATOR_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_BYTE_RATE_ESTIMATOR_H_

#include <deque>
#include <optional>

#include "lib/zx/time.h"
#include "zircon/types.h"

namespace media_player {

// Estimates the byte rate of some operation using the provided samples. E.g.
// read operations from a file or download.
class ByteRateEstimator {
 public:
  // Describes an instance of the measured operation (e.g. a read of one chunk
  // from file). Times should come from |zx_clock_get_monotonic|. See
  // //docs/concepts/kernel/time.md.
  struct ByteRateSample {
    zx::time start_time;
    zx::time stop_time;
    size_t bytes_processed;
  };

  // |ByteRateSampler| is an API for recording |ByteRateSample|s.
  class ByteRateSampler {
   public:
    ByteRateSampler(ByteRateSampler&& rvalue) = default;
    ByteRateSampler& operator=(ByteRateSampler&&) = default;

    // Starts a timed sample of an operation on |bytes| bytes.
    static ByteRateSampler StartSample(size_t bytes);

    // Stops timing and returns a finished sample.
    static ByteRateSample FinishSample(ByteRateSampler sampler);

   private:
    explicit ByteRateSampler(ByteRateSample started_sample);

    ByteRateSample sample_;

    // Disallow copy and assign.
    ByteRateSampler(const ByteRateSampler&) = delete;
    ByteRateSampler& operator=(const ByteRateSampler&) = delete;
  };

  explicit ByteRateEstimator(size_t max_sample_count);

  // Adds a sample for the byte rate estimation.
  void AddSample(const ByteRateSample& sample);

  // Estimates the bytes per second of the operation this instance has sampled
  // using a weighted moving average of at most |max_sample_count| samples.
  std::optional<float> Estimate();

 private:
  std::deque<zx::duration> samples_;
  size_t max_sample_count_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_BYTE_RATE_ESTIMATOR_H_
