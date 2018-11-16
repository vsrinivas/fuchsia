// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>
#include <math.h>
#include <string.h>

#include <cobalt-client/cpp/histogram.h>

#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/metric-options.h>
#include <fbl/limits.h>
#include <fuchsia/cobalt/c/fidl.h>

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
    ZX_DEBUG_ASSERT(unshifted_bucket >= fbl::numeric_limits<uint32_t>::min());
    ZX_DEBUG_ASSERT(unshifted_bucket <= fbl::numeric_limits<uint32_t>::max());
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

} // namespace

void InitBucketBuffer(HistogramBucket* buckets, uint32_t bucket_count) {
    for (uint32_t i = 0; i < bucket_count; ++i) {
        buckets[i].count = 0;
        buckets[i].index = i;
    }
}

void InitLazily(const MetricOptions& options, HistogramBucket* buckets, uint32_t num_buckets,
                RemoteMetricInfo* metric_info) {
    ZX_DEBUG_ASSERT(!options.IsLazy());
    *metric_info = RemoteMetricInfo::From(options);
    InitBucketBuffer(buckets, num_buckets);
}

bool HistogramFlush(const RemoteMetricInfo& metric_info, Logger* logger,
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

} // namespace internal

HistogramOptions::HistogramOptions(const HistogramOptions&) = default;

HistogramOptions HistogramOptions::Exponential(uint32_t bucket_count, uint32_t base,
                                               uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.base = base;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset - scalar);
    options.type = Type::kExponential;
    internal::LoadExponential(bucket_count, &options);
    return options;
}

HistogramOptions HistogramOptions::Linear(uint32_t bucket_count, uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset);
    options.type = Type::kLinear;
    internal::LoadLinear(bucket_count, &options);
    return options;
}

} // namespace cobalt_client
