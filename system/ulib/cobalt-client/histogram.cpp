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

double GetLinearBucketValue(uint32_t bucket_index, const HistogramOptions& options) {
    if (bucket_index == 0) {
        return -DBL_MAX;
    }
    return options.scalar * (bucket_index - 1) + options.offset;
}

double GetExponentialBucketValue(uint32_t bucket_index, const HistogramOptions& options) {
    if (bucket_index == 0) {
        return -DBL_MAX;
    }
    return options.scalar * pow(options.base, bucket_index - 1) + options.offset;
}

uint32_t GetLinearBucket(double value, const HistogramOptions& options, double max_value) {
    if (value < options.offset) {
        return 0;
    } else if (value >= max_value) {
        return options.bucket_count + 1;
    }
    double unshifted_bucket = (value - options.offset) / options.scalar;
    ZX_DEBUG_ASSERT(unshifted_bucket >= fbl::numeric_limits<uint32_t>::min());
    ZX_DEBUG_ASSERT(unshifted_bucket <= fbl::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(unshifted_bucket) + 1;
}

uint32_t GetExponentialBucket(double value, const HistogramOptions& options, double max_value) {
    if (value < options.scalar + options.offset) {
        return 0;
    } else if (value >= max_value) {
        return options.bucket_count + 1;
    }

    // Use bigger size double to perform the calculations to avoid precision errors near boundaries.
    double diff = value - options.offset;
    uint32_t unshifted_bucket = 0;
    // Only use the formula if the difference is positive.
    if (diff >= options.scalar) {
        unshifted_bucket =
            static_cast<uint32_t>(floor((log2(diff) - log2(options.scalar)) / log2(options.base)));
    }
    ZX_DEBUG_ASSERT(unshifted_bucket <= options.bucket_count + 1);

    double lower_bound = GetExponentialBucketValue(unshifted_bucket + 1, options);
    if (lower_bound > value) {
        --unshifted_bucket;
    }
    return unshifted_bucket + 1;
}

void LoadExponential(HistogramOptions* options) {
    options->max_value =
        options->scalar * pow(options->base, options->bucket_count) + options->offset;
    options->map_fn = [](double val, const HistogramOptions& options) {
        return internal::GetExponentialBucket(val, options, options.max_value);
    };
    options->reverse_map_fn = internal::GetExponentialBucketValue;
}

void LoadLinear(HistogramOptions* options) {
    options->max_value =
        static_cast<double>(options->scalar * options->bucket_count + options->offset);
    options->map_fn = [](double val, const HistogramOptions& options) {
        return internal::GetLinearBucket(val, options, options.max_value);
    };
    options->reverse_map_fn = internal::GetLinearBucketValue;
}

} // namespace

BaseHistogram::BaseHistogram(uint32_t num_buckets) {
    buckets_.reserve(num_buckets);
    for (uint32_t i = 0; i < num_buckets; ++i) {
        buckets_.push_back(BaseCounter());
    }
}

BaseHistogram::BaseHistogram(BaseHistogram&& other) = default;

RemoteHistogram::RemoteHistogram(uint32_t num_buckets, const RemoteMetricInfo& metric_info,
                                 RemoteHistogram::EventBuffer buffer)
    : BaseHistogram(num_buckets), buffer_(fbl::move(buffer)), metric_info_(metric_info) {
    bucket_buffer_.reserve(num_buckets);
    for (uint32_t i = 0; i < num_buckets; ++i) {
        HistogramBucket bucket;
        bucket.count = 0;
        bucket.index = i;
        bucket_buffer_.push_back(bucket);
    }
    auto* buckets = buffer_.mutable_event_data();
    buckets->set_data(bucket_buffer_.get());
    buckets->set_count(bucket_buffer_.size());
}

RemoteHistogram::RemoteHistogram(RemoteHistogram&& other)
    : BaseHistogram(fbl::move(other)), bucket_buffer_(fbl::move(other.bucket_buffer_)),
      buffer_(fbl::move(other.buffer_)), metric_info_(other.metric_info_) {}

bool RemoteHistogram::Flush(const RemoteHistogram::FlushFn& flush_handler) {
    if (!buffer_.TryBeginFlush()) {
        return false;
    }

    // Sets every bucket back to 0, not all buckets will be at the same instant, but
    // eventual consistency in the backend is good enough.
    for (uint32_t bucket_index = 0; bucket_index < bucket_buffer_.size(); ++bucket_index) {
        bucket_buffer_[bucket_index].count = buckets_[bucket_index].Exchange();
    }

    flush_handler(metric_info_, buffer_, fbl::BindMember(&buffer_, &EventBuffer::CompleteFlush));
    return true;
}
} // namespace internal

HistogramOptions::HistogramOptions(const HistogramOptions&) = default;

HistogramOptions HistogramOptions::Exponential(uint32_t bucket_count, uint32_t base,
                                               uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.bucket_count = bucket_count;
    options.base = base;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset - scalar);
    options.type = Type::kExponential;
    internal::LoadExponential(&options);
    return options;
}

HistogramOptions HistogramOptions::Linear(uint32_t bucket_count, uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.bucket_count = bucket_count;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset);
    options.type = Type::kLinear;
    internal::LoadLinear(&options);
    return options;
}

Histogram::Histogram(HistogramOptions* options, internal::RemoteHistogram* remote_histogram)
    : options_(options), remote_histogram_(remote_histogram) {}

Histogram::Histogram(const Histogram&) = default;
Histogram::Histogram(Histogram&&) = default;
Histogram& Histogram::operator=(const Histogram&) = default;
Histogram& Histogram::operator=(Histogram&&) = default;
Histogram::~Histogram() = default;

template <typename ValueType>
void Histogram::Add(ValueType value, Histogram::Count times) {
    double dbl_value = static_cast<double>(value);
    uint32_t bucket = options_->map_fn(dbl_value, *options_);
    remote_histogram_->IncrementCount(bucket, times);
}

template <typename ValueType>
Histogram::Count Histogram::GetRemoteCount(ValueType value) const {
    double dbl_value = static_cast<double>(value);
    uint32_t bucket = options_->map_fn(dbl_value, *options_);
    return remote_histogram_->GetCount(bucket);
}

// Supported template instantiations.
template void Histogram::Add<double>(double, Histogram::Count);
template void Histogram::Add<int32_t>(int32_t, Histogram::Count);
template void Histogram::Add<uint32_t>(uint32_t, Histogram::Count);
template void Histogram::Add<int64_t>(int64_t, Histogram::Count);
template void Histogram::Add<uint64_t>(uint64_t, Histogram::Count);

template Histogram::Count Histogram::GetRemoteCount<double>(double) const;
template Histogram::Count Histogram::GetRemoteCount<int32_t>(int32_t) const;
template Histogram::Count Histogram::GetRemoteCount<uint32_t>(uint32_t) const;
template Histogram::Count Histogram::GetRemoteCount<int64_t>(int64_t) const;
template Histogram::Count Histogram::GetRemoteCount<uint64_t>(uint64_t) const;

} // namespace cobalt_client
