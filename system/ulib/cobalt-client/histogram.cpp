// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/histogram-internal.h>
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
} // namespace cobalt_client
