// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// BaseCounter and RemoteCounter differ in that the first is simply a thin wrapper over
// an atomic while the second provides Cobalt Fidl specific API and holds more metric related
// data for a full fledged metric.
//
// Thin wrapper on top of an atomic, which provides a fixed memory ordering for all calls.
// Calls are inlined to reduce overhead.A
template <typename T>
class BaseCounter {
 public:
  // Alias for the underlying counter type.
  using Type = T;

  // All atomic operations use this memory order.
  static constexpr auto kMemoryOrder = std::memory_order_relaxed;

  BaseCounter() : counter_(0) {}
  BaseCounter(const BaseCounter&) = delete;
  BaseCounter(BaseCounter&& other) : counter_(other.Exchange(0)) {}
  BaseCounter& operator=(const BaseCounter&) = delete;
  BaseCounter& operator=(BaseCounter&&) = delete;
  ~BaseCounter() = default;

  // Increments |counter_| by |val|.
  void Increment(Type val = 1) { counter_.fetch_add(val, kMemoryOrder); }

  // Returns the current value of|counter_| and resets it to |val|.
  Type Exchange(Type val = 0) { return counter_.exchange(val, kMemoryOrder); }

  // Returns the current value of |counter_|.
  Type Load() const { return counter_.load(kMemoryOrder); }

 protected:
  static_assert(std::is_integral<Type>::value, "Can only count integral types");

  std::atomic<Type> counter_;
};

// Counter which represents a standalone cobalt metric. Provides API for converting
// to cobalt FIDL types.
//
// This class is moveable and move-assignable.
// This class is not copy or copy-assignable.
// This class is thread-safe except for |Flushing| which is thread-compatible.
class RemoteCounter : public BaseCounter<int64_t>, public FlushInterface {
 public:
  RemoteCounter() = default;
  RemoteCounter(const MetricInfo& metric_info);
  RemoteCounter(const RemoteCounter&) = delete;
  RemoteCounter(RemoteCounter&&);
  RemoteCounter& operator=(const RemoteCounter&) = delete;
  RemoteCounter& operator=(RemoteCounter&&) = delete;
  ~RemoteCounter() override = default;

  bool Flush(Logger* logger) override;

  void UndoFlush() override;

  // Should only be called when default constructed.
  void Initialize(const MetricOptions& metric_options);

  // Returns the metric_id associated with this remote metric.
  const MetricInfo& metric_info() const { return metric_info_; }

 private:
  int64_t buffer_;
  // Unique-Id representing this metric in the backend.
  MetricInfo metric_info_;
};

}  // namespace internal
}  // namespace cobalt_client
