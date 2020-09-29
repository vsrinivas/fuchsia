// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_HISTOGRAM_H_
#define COBALT_CLIENT_CPP_HISTOGRAM_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/histogram_internal.h>
#include <cobalt-client/cpp/metric_options.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {

// Thin wrapper for a histogram. This class does not own the data, but acts as a proxy.
//
// This class is not copyable, moveable or assignable.
// This class is thread-safe.
template <uint32_t num_buckets>
class Histogram {
 public:
  static_assert(num_buckets > 0, "num_buckets must be postive.");

  // Underlying type used for representing bucket count.
  using Count = uint64_t;

  Histogram() = default;
  explicit Histogram(const HistogramOptions& options) : remote_histogram_(options) {}
  // Collector's lifetime must exceed the histogram's lifetime.
  Histogram(const HistogramOptions& options, Collector* collector)
      : remote_histogram_(options), collector_(collector) {
    if (collector_ != nullptr) {
      collector_->Subscribe(&remote_histogram_.value());
    }
  }
  // Constructor for internal use only.
  Histogram(const HistogramOptions& options, internal::FlushInterface** flush_interface)
      : remote_histogram_(options) {
    *flush_interface = &remote_histogram_.value();
  }
  Histogram(const Histogram&) = delete;
  Histogram(Histogram&& other) = delete;
  Histogram& operator=(const Histogram&) = delete;
  Histogram& operator=(Histogram&&) = delete;
  ~Histogram() {
    if (collector_ != nullptr && remote_histogram_.has_value()) {
      collector_->UnSubscribe(&remote_histogram_.value());
    }
  }

  // Optionally initialize lazily the histogram, if is more readable to do so
  // in the constructor or function body.
  void Initialize(const HistogramOptions& options, Collector* collector) {
    ZX_DEBUG_ASSERT_MSG(!remote_histogram_.has_value(),
                        "Cannot call |Initialize| on intialized Histogram.");
    collector_ = collector;
    remote_histogram_.emplace(options);
    if (collector_ != nullptr) {
      collector_->Subscribe(&remote_histogram_.value());
    }
  }

  // Returns the number of buckets allocated for this histogram. This includes
  // the overflow and underflow buckets.
  constexpr uint32_t size() const { return num_buckets + 2; }

  // Increases the count of the bucket containing |value| by |times|.
  // |ValueType| must either be an (u)int or a double.
  template <typename ValueType>
  void Add(ValueType value, Count times = 1) {
    ZX_DEBUG_ASSERT_MSG(remote_histogram_.has_value(),
                        "Must initialize histogram before calling |Add|.");
    double dbl_value = static_cast<double>(value);
    uint32_t bucket = remote_histogram_->metric_options().map_fn(
        dbl_value, size(), remote_histogram_->metric_options());
    remote_histogram_->IncrementCount(bucket, times);
  }

  // Returns the count of the bucket containing |value|, since it was last sent
  // to cobalt.
  // |ValueType| must either be an (u)int or a double.
  template <typename ValueType>
  Count GetCount(ValueType value) const {
    ZX_DEBUG_ASSERT_MSG(remote_histogram_.has_value(),
                        "Must initialize histogram before calling |GetCount|.");
    double dbl_value = static_cast<double>(value);
    uint32_t bucket = remote_histogram_->metric_options().map_fn(
        dbl_value, size(), remote_histogram_->metric_options());
    return remote_histogram_->GetCount(bucket);
  }

  // Returns the set of |HistogramOptions| used to construc this histogram.
  const HistogramOptions& GetOptions() const {
    ZX_DEBUG_ASSERT_MSG(remote_histogram_.has_value(),
                        "Must initialize histogram before calling |GetOptions|.");
    return remote_histogram_->metric_options();
  }

 private:
  // Two extra buckets for overflow and underflow buckets.
  std::optional<internal::RemoteHistogram<num_buckets + 2>> remote_histogram_;
  Collector* collector_ = nullptr;
};
}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_HISTOGRAM_H_
