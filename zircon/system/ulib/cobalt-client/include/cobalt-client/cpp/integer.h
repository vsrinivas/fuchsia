// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_INTEGER_H_
#define COBALT_CLIENT_CPP_INTEGER_H_

#include <cstdint>
#include <optional>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter_internal.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {

namespace internal {
// Integer which represents a standalone cobalt metric. Provides API for converting
// to cobalt FIDL types.
//
// This class is moveable and move-assignable.
// This class is not copy or copy-assignable.
// This class is thread-safe except for |Flushing| which is thread-compatible.
class RemoteInteger : public BaseCounter<int64_t>, public FlushInterface {
 public:
  RemoteInteger() = default;
  explicit RemoteInteger(const MetricOptions& metric_options);
  RemoteInteger(const RemoteInteger&) = delete;
  RemoteInteger(RemoteInteger&&) noexcept;
  RemoteInteger& operator=(const RemoteInteger&) = delete;
  RemoteInteger& operator=(RemoteInteger&&) = delete;
  ~RemoteInteger() override = default;

  bool Flush(Logger* logger) override;

  void UndoFlush() override;

  // Returns the metric_id associated with this remote metric.
  const MetricOptions& metric_options() const { return metric_options_; }

 private:
  int64_t buffer_;
  MetricOptions metric_options_;
};

}  // namespace internal

// Thin wrapper for an atomic integer with a fixed memory order. The integer handles
// a remote integer/memory-usage and a local integer. The remote integer is periodically flushed,
// while the local integer is viewed on demand (and optionally flushed depending on configuration).
//
// This class not copyable, moveable and assignable.
// This class is thread-safe.
class Integer {
 public:
  // Underlying type used for representing an actual integer.
  using Int = uint64_t;

  Integer() = default;
  explicit Integer(const MetricOptions& options);
  Integer(const MetricOptions& options, Collector* collector);
  // Constructor for internal use only.
  Integer(const MetricOptions& options, internal::FlushInterface** flush_interface);
  Integer(const Integer& other) = delete;
  Integer(Integer&&) = delete;
  ~Integer();

  // Optionally initialize lazily the integer, if is more readable to do so
  // in the constructor or function body.
  void Initialize(const MetricOptions& options, Collector* collector);

  // Increments the integer value by |value|. This applies to local and remote
  // values of the integer.
  void Set(Int value);

  // Returns the current value of the integer that would be
  // sent to the remote service(cobalt).
  Int Get() const;

  // Returns the set of |MetricOptions| used to construc this histogram.
  const MetricOptions& GetOptions() const {
    ZX_DEBUG_ASSERT_MSG(remote_integer_.has_value(),
                        "Must initialize integer before calling |Get|.");
    return remote_integer_->metric_options();
  }

 private:
  std::optional<internal::RemoteInteger> remote_integer_;
  Collector* collector_ = nullptr;
};

}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_INTEGER_H_
