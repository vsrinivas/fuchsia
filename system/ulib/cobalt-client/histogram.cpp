// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/observation.h>
#include <fuchsia/cobalt/c/fidl.h>

namespace cobalt_client {
namespace internal {
namespace {

ObservationValue MakeHistogramObservation(const fbl::String& name, uint32_t encoding_id,
                                          uint64_t buckets, DistributionEntry* entries) {
    ObservationValue value;
    value.encoding_id = encoding_id;
    value.name.size = name.empty() ? 0 : name.size();
    value.name.data = const_cast<char*>(name.data());
    value.value = ::cobalt_client::BucketDistributionValue(buckets, entries);
    return value;
}
} // namespace

BaseHistogram::BaseHistogram(const fbl::String& name, const fbl::Vector<ObservationValue>& metadata,
                             size_t buckets, uint64_t metric_id, uint32_t encoding_id)
    : name_(name), metric_id_(metric_id), encoding_id_(encoding_id), flushing_(false) {

    observations_.reserve(metadata.size() + 1);
    for (const auto& obs : metadata) {
        observations_.push_back(obs);
    }

    buckets_.reserve(buckets);
    buffer_.reserve(buckets);
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        buckets_.push_back(Counter(metric_id, encoding_id));
        buffer_.push_back({/*index=*/0, 0});
    }

    observations_.push_back(MakeHistogramObservation(name_, encoding_id_, buckets, buffer_.get()));
}

BaseHistogram::BaseHistogram(BaseHistogram&& other)
    : buckets_(fbl::move(other.buckets_)), observations_(fbl::move(other.observations_)),
      buffer_(fbl::move(other.buffer_)), name_(other.name_), metric_id_(other.metric_id_),
      encoding_id_(other.encoding_id_), flushing_(other.flushing_.load()) {}

bool BaseHistogram::Flush(const BaseHistogram::FlushFn& flush_handler) {
    // Set flushing_ to true. If it was already flushing, then do nothing.
    if (flushing_.exchange(true, fbl::memory_order::memory_order_relaxed)) {
        return false;
    }

    for (size_t bucket_index = 0; bucket_index < buckets_.size(); ++bucket_index) {
        buffer_[bucket_index].index = static_cast<uint32_t>(bucket_index);
        buffer_[bucket_index].count = buckets_[bucket_index].Exchange(0);
    }
    fidl::VectorView<ObservationValue> values;
    values.set_data(observations_.get());
    values.set_count(observations_.size());

    flush_handler(metric_id_, values,
                  [this]() { flushing_.store(false, fbl::memory_order::memory_order_relaxed); });
    return true;
}

} // namespace internal
} // namespace cobalt_client
