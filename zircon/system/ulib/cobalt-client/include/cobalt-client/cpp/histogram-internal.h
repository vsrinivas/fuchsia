// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace cobalt_client {
namespace internal {

// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// Base class for histogram, that provides a thin layer over a collection of buckets
// that represent a histogram. Once constructed, unless moved, the class is thread-safe.
// All allocations happen when constructed.
//
// This class is not moveable, not copyable or assignable.
// This class is thread-compatible.
template <uint32_t num_buckets>
class BaseHistogram {
 public:
  using Count = uint64_t;
  using Bucket = uint32_t;

  BaseHistogram() = default;
  BaseHistogram(const BaseHistogram&) = delete;
  BaseHistogram(BaseHistogram&&) = delete;
  BaseHistogram& operator=(const BaseHistogram&) = delete;
  BaseHistogram& operator=(BaseHistogram&&) = delete;
  ~BaseHistogram() = default;

  // Returns the number of buckets of this histogram.
  constexpr uint32_t size() const { return num_buckets; }

  void IncrementCount(Bucket bucket, Count val = 1) {
    ZX_DEBUG_ASSERT_MSG(bucket < size(), "IncrementCount bucket(%u) out of range(%u).", bucket,
                        size());
    buckets_[bucket].Increment(val);
  }

  Count GetCount(uint32_t bucket) const {
    ZX_DEBUG_ASSERT_MSG(bucket < size(), "GetCount bucket out of range.");
    return buckets_[bucket].Load();
  }

 protected:
  // Counter for the abs frequency of every histogram bucket.
  BaseCounter<uint64_t> buckets_[num_buckets];
};

// Free functions to move logic outside the templated class.

// Initializes buckets such that bucket[i].index = i and bucket[i].count = 0.
void InitBucketBuffer(HistogramBucket* buckets, uint32_t bucket_count);

// Sets |metric_info| to respective values from |options|, and initializes the buckets.
void InitLazily(const MetricOptions& options, HistogramBucket* buckets, uint32_t num_buckets,
                MetricInfo* metric_info);

// Sets the count of each bucket in |bucket_buffer| to the respective value in
// |buckets|, and sets the count in |buckets| to 0.
bool HistogramFlush(const MetricInfo& metric_info, Logger* logger, BaseCounter<uint64_t>* buckets,
                    HistogramBucket* bucket_buffer, uint32_t num_buckets);

// Undo's an ungoing Flush effects.
void HistogramUndoFlush(BaseCounter<uint64_t>* buckets, HistogramBucket* bucket_buffer,
                        uint32_t num_buckets);

// This class provides a histogram which represents a full fledged cobalt metric. The histogram
// owner will call |Flush| which is meant to incrementally persist data to cobalt.
//
// This class is not moveable, copyable or assignable.
// This class is thread-compatible.
template <uint32_t num_buckets>
class RemoteHistogram : public BaseHistogram<num_buckets>, public FlushInterface {
 public:
  RemoteHistogram() = default;
  RemoteHistogram(const MetricInfo& metric_info)
      : BaseHistogram<num_buckets>(), metric_info_(metric_info) {
    InitBucketBuffer(bucket_buffer_, num_buckets);
  }
  RemoteHistogram(const RemoteHistogram&) = delete;
  RemoteHistogram(RemoteHistogram&&) = delete;
  RemoteHistogram& operator=(const RemoteHistogram&) = delete;
  RemoteHistogram& operator=(RemoteHistogram&&) = delete;
  ~RemoteHistogram() override = default;

  void Initialize(const MetricOptions& options) {
    InitLazily(options, bucket_buffer_, num_buckets, &metric_info_);
  }

  bool Flush(Logger* logger) override {
    return HistogramFlush(metric_info_, logger, this->buckets_, bucket_buffer_, num_buckets);
  }

  void UndoFlush() override { HistogramUndoFlush(this->buckets_, bucket_buffer_, num_buckets); }

  // Returns the metric_id associated with this remote metric.
  const MetricInfo& metric_info() const { return metric_info_; }

 private:
  // Buffer for out of line allocation for the data being sent
  // through fidl. This buffer is rewritten on every flush, and contains
  // an entry for each bucket.
  HistogramBucket bucket_buffer_[num_buckets];

  // Metric information such as metric_id, event_code and component.
  MetricInfo metric_info_;
};

}  // namespace internal
}  // namespace cobalt_client
