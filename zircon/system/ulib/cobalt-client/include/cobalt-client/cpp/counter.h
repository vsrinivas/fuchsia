// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_COUNTER_H_
#define COBALT_CLIENT_CPP_COUNTER_H_

#include <cstdint>
#include <optional>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter_internal.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {

// Thin wrapper for an atomic counter with a fixed memory order. The counter handles
// a remote count and a local count. The remote count is periodically flushed, while
// the local count is viewed on demand (and optionally flushed depending on configuration).
//
// This class is copyable, moveable and assignable.
// This class is thread-safe.
class Counter {
 public:
  // Underlying type used for representing an actual counter.
  using Count = uint64_t;

  Counter() = default;
  explicit Counter(const MetricOptions& options);
  Counter(const MetricOptions& options, Collector* collector);
  // Constructor for internal use only.
  Counter(const MetricOptions& options, internal::FlushInterface** flush_interface);
  Counter(const Counter& other) = delete;
  Counter(Counter&&) = delete;
  ~Counter();

  // Optionally initialize lazily the counter, if is more readable to do so
  // in the constructor or function body.
  void Initialize(const MetricOptions& options, Collector* collector);

  // Increments the counter value by |value|. This applies to local and remote
  // values of the counter.
  void Increment(Count value = 1);

  // Returns the current value of the counter that would be
  // sent to the remote service(cobalt).
  Count GetCount() const;

  // Returns the set of |MetricOptions| used to construc this histogram.
  const MetricOptions& GetOptions() const {
    ZX_DEBUG_ASSERT_MSG(remote_counter_.has_value(),
                        "Must initialize counter before calling |GetOptions|.");
    return remote_counter_->metric_options();
  }

 private:
  std::optional<internal::RemoteCounter> remote_counter_ = std::nullopt;
  Collector* collector_ = nullptr;
};

}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_COUNTER_H_
