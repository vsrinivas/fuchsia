// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/histogram.h>

#include <float.h>

#include <cmath>
#include <cstring>
#include <limits>

#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/metric-options.h>

namespace cobalt_client {
namespace internal {
namespace {

double GetLinearBucketValue(uint32_t bucket_index, uint32_t bucket_count,
                            const HistogramOptions& options) {
  if (bucket_index == 0) {
    return -DBL_MAX;
  }
  return options.scalar * (bucket_index - 1) + options.offset;
}

double GetExponentialBucketValue(uint32_t bucket_index, uint32_t bucket_count,
                                 const HistogramOptions& options) {
  if (bucket_index == 0) {
    return -DBL_MAX;
  }
  return options.scalar * pow(options.base, bucket_index - 1) + options.offset;
}

uint32_t GetLinearBucket(double value, uint32_t bucket_count, const HistogramOptions& options,
                         double max_value) {
  if (value < options.offset) {
    return 0;
  } else if (value >= max_value) {
    return bucket_count - 1;
  }
  double unshifted_bucket = (value - options.offset) / options.scalar;
  ZX_DEBUG_ASSERT(unshifted_bucket >= std::numeric_limits<uint32_t>::min());
  ZX_DEBUG_ASSERT(unshifted_bucket <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(unshifted_bucket) + 1;
}

uint32_t GetExponentialBucket(double value, uint32_t bucket_count, const HistogramOptions& options,
                              double max_value) {
  if (value < options.scalar + options.offset) {
    return 0;
  } else if (value >= max_value) {
    return bucket_count - 1;
  }

  double diff = value - options.offset;
  uint32_t unshifted_bucket = 0;
  // Only use the formula if the difference is positive.
  if (diff >= options.scalar) {
    unshifted_bucket =
        static_cast<uint32_t>(floor((log2(diff) - log2(options.scalar)) / log2(options.base)));
  }
  ZX_DEBUG_ASSERT(unshifted_bucket <= bucket_count + 1);

  double lower_bound = GetExponentialBucketValue(unshifted_bucket + 1, bucket_count, options);
  if (lower_bound > value) {
    --unshifted_bucket;
  }
  return unshifted_bucket + 1;
}

void LoadExponential(uint32_t bucket_count, HistogramOptions* options) {
  options->max_value = options->scalar * pow(options->base, bucket_count) + options->offset;
  options->map_fn = [](double val, uint32_t bucket_count, const HistogramOptions& options) {
    return internal::GetExponentialBucket(val, bucket_count, options, options.max_value);
  };
  options->reverse_map_fn = internal::GetExponentialBucketValue;
}

void LoadLinear(uint32_t bucket_count, HistogramOptions* options) {
  options->max_value = static_cast<double>(options->scalar * bucket_count + options->offset);
  options->map_fn = [](double val, uint32_t bucket_count, const HistogramOptions& options) {
    return internal::GetLinearBucket(val, bucket_count, options, options.max_value);
  };
  options->reverse_map_fn = internal::GetLinearBucketValue;
}

}  // namespace

void InitBucketBuffer(HistogramBucket* buckets, uint32_t bucket_count) {
  for (uint32_t i = 0; i < bucket_count; ++i) {
    buckets[i].count = 0;
    buckets[i].index = i;
  }
}

bool HistogramFlush(const HistogramOptions& metric_info, Logger* logger,
                    BaseCounter<uint64_t>* buckets, HistogramBucket* bucket_buffer,
                    uint32_t num_buckets) {
  // Sets every bucket back to 0, not all buckets will be at the same instant, but
  // eventual consistency in the backend is good enough.
  for (uint32_t bucket_index = 0; bucket_index < num_buckets; ++bucket_index) {
    bucket_buffer[bucket_index].count = buckets[bucket_index].Exchange();
  }
  return logger->Log(metric_info, bucket_buffer, num_buckets);
}

void HistogramUndoFlush(BaseCounter<uint64_t>* buckets, HistogramBucket* bucket_buffer,
                        uint32_t num_buckets) {
  for (uint32_t bucket_index = 0; bucket_index < num_buckets; ++bucket_index) {
    buckets[bucket_index].Increment(bucket_buffer[bucket_index].count);
  }
}

}  // namespace internal

HistogramOptions::HistogramOptions(const HistogramOptions&) = default;
HistogramOptions& HistogramOptions::operator=(const HistogramOptions&) = default;

HistogramOptions HistogramOptions::Exponential(uint32_t bucket_count, int64_t max) {
  return Exponential(bucket_count, 0, max);
}

HistogramOptions HistogramOptions::Exponential(uint32_t bucket_count, int64_t min, int64_t max) {
  ZX_DEBUG_ASSERT_MSG(min < max, "min must be smaller than max.");
  // 2^bucket - 1 is the lower bound of the overflow bucket.
  uint64_t overflow_limit = (1 << bucket_count) - 1;
  uint64_t range = max - min;
  uint32_t scalar = 1;

  // If max is past the overflow limit we need to scale up, and the minimum is 1.
  if (range - overflow_limit > 0) {
    scalar = static_cast<uint32_t>(range / overflow_limit);
    if (range % overflow_limit != 0) {
      scalar++;
    }
  }
  ZX_DEBUG_ASSERT_MSG(2 * range >= scalar * overflow_limit,
                      "range is too small for the number of buckets.");

  return CustomizedExponential(bucket_count, 2, scalar, min);
}

HistogramOptions HistogramOptions::CustomizedExponential(uint32_t bucket_count, uint32_t base,
                                                         uint32_t scalar, int64_t min) {
  HistogramOptions options;
  options.type = Type::kExponential;
  options.base = static_cast<double>(base);
  options.scalar = static_cast<double>(scalar);
  options.offset = static_cast<double>(min - scalar);
  internal::LoadExponential(bucket_count, &options);
  return options;
}

HistogramOptions HistogramOptions::Linear(uint32_t bucket_count, int64_t max) {
  return Linear(bucket_count, 0, max);
}

HistogramOptions HistogramOptions::Linear(uint32_t bucket_count, int64_t min, int64_t max) {
  ZX_DEBUG_ASSERT_MSG(min < max, "min must be smaller than max.");
  uint64_t range = max - min;
  ZX_DEBUG_ASSERT_MSG(range >= bucket_count, "range is too small for the number of buckets.");
  uint64_t scalar = range / bucket_count;
  if (range % bucket_count != 0) {
    scalar++;
  }
  ZX_DEBUG_ASSERT_MSG(scalar <= std::numeric_limits<uint32_t>::max(), "scalar overflow\n");
  return CustomizedLinear(bucket_count, static_cast<uint32_t>(scalar), min);
}

HistogramOptions HistogramOptions::CustomizedLinear(uint32_t bucket_count, uint32_t step_size,
                                                    int64_t min) {
  HistogramOptions options;
  options.scalar = static_cast<double>(step_size);
  options.offset = static_cast<double>(min);
  options.type = Type::kLinear;
  internal::LoadLinear(bucket_count, &options);
  return options;
}

bool HistogramOptions::IsValid() const {
  switch (type) {
    case Type::kExponential:
      if (base == 0) {
        return false;
      }
      __FALLTHROUGH;
    case Type::kLinear:
      if (scalar == 0) {
        return false;
      }
      break;
  }

  return true;
}

}  // namespace cobalt_client
