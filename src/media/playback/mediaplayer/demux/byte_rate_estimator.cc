// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_rate_estimator.h"

#include "src/lib/fxl/logging.h"

namespace media_player {

// static
ByteRateEstimator::ByteRateSampler
ByteRateEstimator::ByteRateSampler::StartSample(size_t bytes) {
  return ByteRateEstimator::ByteRateSampler(ByteRateSample{
      .start_time = zx::clock::get_monotonic(),
      .stop_time = zx::time(0),
      .bytes_processed = bytes,
  });
}

// static
ByteRateEstimator::ByteRateSample
ByteRateEstimator::ByteRateSampler::FinishSample(
    ByteRateEstimator::ByteRateSampler sampler) {
  sampler.sample_.stop_time = zx::clock::get_monotonic();
  return sampler.sample_;
}

ByteRateEstimator::ByteRateSampler::ByteRateSampler(
    ByteRateEstimator::ByteRateSample started_sample)
    : sample_(started_sample) {}

ByteRateEstimator::ByteRateEstimator(size_t max_sample_count)
    : max_sample_count_(max_sample_count) {}

std::optional<float> ByteRateEstimator::Estimate() {
  if (samples_.empty()) {
    return std::nullopt;
  }

  zx::duration numerator;
  int64_t n = std::min(samples_.size(), max_sample_count_);
  for (uint64_t i = 0; i < samples_.size(); ++i) {
    numerator += samples_[i] * (n - i);
  }
  int64_t denominator = n * (n + 1) / 2;
  FXL_DCHECK(denominator != 0);
  zx::duration time_per_byte = numerator / denominator;
  return float(ZX_SEC(1)) / float(time_per_byte.to_nsecs());
}

void ByteRateEstimator::AddSample(const ByteRateSample& sample) {
  FXL_DCHECK(sample.stop_time > sample.start_time);
  zx::duration duration = sample.stop_time - sample.start_time;
  zx::duration time_per_byte = duration / sample.bytes_processed;
  samples_.push_front(time_per_byte);
  if (samples_.size() > max_sample_count_) {
    samples_.pop_back();
  }
}

}  // namespace media_player