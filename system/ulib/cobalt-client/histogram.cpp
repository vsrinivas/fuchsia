// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <string.h>

#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/histogram-options.h>
#include <fbl/limits.h>
#include <fuchsia/cobalt/c/fidl.h>

namespace cobalt_client {
namespace internal {
namespace {

ObservationValue MakeHistogramObservation(const fbl::String& name, uint32_t encoding_id,
                                          uint64_t buckets, BucketDistributionEntry* entries) {
    ObservationValue value;
    value.encoding_id = encoding_id;
    value.name.size = name.empty() ? 0 : name.size();
    value.name.data = const_cast<char*>(name.data());
    value.value = BucketDistributionValue(buckets, entries);
    return value;
}

double GetLinearBucketValue(uint32_t bucket_index, const HistogramOptions& options) {
    if (bucket_index == 0) {
        return -INFINITY;
    }
    return options.scalar * (bucket_index - 1) + options.offset;
}

double GetExponentialBucketValue(uint32_t bucket_index, const HistogramOptions& options) {
    if (bucket_index == 0) {
        return -INFINITY;
    }
    return options.scalar * pow(options.base, bucket_index - 1) + options.offset;
}

uint32_t GetLinearBucket(double value, const HistogramOptions& options, double max_value) {
    if (value < options.offset) {
        return 0;
    } else if (value >= max_value) {
        return options.bucket_count + 1;
    }
    double tmp = (value - options.offset) / options.scalar;
    ZX_DEBUG_ASSERT(tmp >= fbl::numeric_limits<uint32_t>::min());
    ZX_DEBUG_ASSERT(tmp <= fbl::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(tmp) + 1;
}

uint32_t GetExponentialBucket(double value, const HistogramOptions& options, double max_value) {
    if (value < options.offset + options.scalar) {
        return 0;
    } else if (value >= max_value) {
        return options.bucket_count + 1;
    }
    // Use bigger size double to perform the calculations to avoid precision errors near boundaries.
    long double tmp = static_cast<long double>(value) - options.offset;
    if (tmp >= options.scalar) {
        tmp = floorl((log2l(tmp) - log2l(options.scalar))) / log2l(options.base);
    } else {
        // Any values close to the offset, but when the diff between the value and the offset
        // is smaller than options.scalar, will be a negative value. Avoid any erros, and just
        // map them to the first unshifted bucket.
        tmp = 0;
    }
    ZX_DEBUG_ASSERT(tmp >= fbl::numeric_limits<uint32_t>::min());
    ZX_DEBUG_ASSERT(tmp <= fbl::numeric_limits<uint32_t>::max());

    // Evaluate near boundary, just in case we are under precision error.
    if (GetExponentialBucketValue(static_cast<uint32_t>(tmp) + 1, options) > value) {
        --tmp;
    }
    // Round to smalled integer and shift the bucket.
    return static_cast<uint32_t>(tmp) + 1;
}

} // namespace

BaseHistogram::BaseHistogram(uint32_t num_buckets) {
    buckets_.reserve(num_buckets);
    for (uint32_t i = 0; i < num_buckets; ++i) {
        buckets_.push_back(BaseCounter());
    }
}

BaseHistogram::BaseHistogram(BaseHistogram&& other) = default;

RemoteHistogram::RemoteHistogram(uint32_t num_buckets, const fbl::String& name, uint64_t metric_id,
                                 uint32_t encoding_id,
                                 const fbl::Vector<ObservationValue>& metadata)
    : BaseHistogram(num_buckets), buffer_(metadata), name_(name), metric_id_(metric_id),
      encoding_id_(encoding_id) {
    bucket_buffer_.reserve(num_buckets);
    for (uint32_t i = 0; i < num_buckets; ++i) {
        bucket_buffer_.push_back({.index = i, .count = 0});
    }
    auto* metric = buffer_.GetMutableMetric();
    metric->encoding_id = encoding_id_;
    metric->name.data = const_cast<char*>(name_.data());
    // Include null termination.
    metric->name.size = name_.size() + 1;
    metric->value = BucketDistributionValue(num_buckets, bucket_buffer_.get());
}

RemoteHistogram::RemoteHistogram(RemoteHistogram&& other)
    : BaseHistogram(fbl::move(other)), buffer_(fbl::move(other.buffer_)),
      bucket_buffer_(fbl::move(other.bucket_buffer_)), name_(fbl::move(other.name_)),
      metric_id_(other.metric_id_), encoding_id_(other.encoding_id_) {}

bool RemoteHistogram::Flush(const RemoteHistogram::FlushFn& flush_handler) {
    if (!buffer_.TryBeginFlush()) {
        return false;
    }

    // Sets every bucket back to 0, not all buckets will be at the same instant, but
    // eventual consistency in the backend is good enough.
    for (uint32_t bucket_index = 0; bucket_index < bucket_buffer_.size(); ++bucket_index) {
        bucket_buffer_[bucket_index].count = buckets_[bucket_index].Exchange();
    }

    flush_handler(metric_id_, buffer_.GetView(),
                  fbl::BindMember(&buffer_, &ObservationBuffer::CompleteFlush));
    return true;
}
} // namespace internal

HistogramOptions HistogramOptions::Exponential(uint32_t bucket_count, uint32_t base,
                                               uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.bucket_count = bucket_count;
    options.base = base;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset - scalar);
    options.type = Type::kExponential;
    double max_value = scalar * pow(base, bucket_count) + options.offset;
    options.map_fn = [max_value](double val, const HistogramOptions& options) {
        return internal::GetExponentialBucket(val, options, max_value);
    };
    options.reverse_map_fn = internal::GetExponentialBucketValue;
    return options;
}

HistogramOptions HistogramOptions::Linear(uint32_t bucket_count, uint32_t scalar, int64_t offset) {
    HistogramOptions options;
    options.bucket_count = bucket_count;
    options.scalar = scalar;
    options.offset = static_cast<double>(offset);
    options.type = Type::kLinear;
    double max_value = static_cast<double>(scalar * bucket_count + offset);
    options.map_fn = [max_value](double val, const HistogramOptions& options) {
        return internal::GetLinearBucket(val, options, max_value);
    };
    options.reverse_map_fn = internal::GetLinearBucketValue;
    return options;
}

} // namespace cobalt_client
