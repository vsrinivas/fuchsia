// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/atomic.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// BaseCounter and RemoteCounter differ in that the first is simply a thin wrapper over
// an atomic while the second provides Cobalt Fidl specific API and holds more metric related
// data for a full fledged metric.
//
// Thin wrapper on top of an atomic, which provides a fixed memory ordering for all calls.
// Calls are inlined to reduce overhead.
class BaseCounter {
public:
    using Type = uint64_t;

    // All atomic operations use this memory order.
    static constexpr fbl::memory_order kMemoryOrder = fbl::memory_order::memory_order_relaxed;

    BaseCounter() : counter_(0) {}
    BaseCounter(const BaseCounter&) = delete;
    BaseCounter(BaseCounter&&);
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
    fbl::atomic<Type> counter_;
};

// Counter which represents a standalone cobalt metric. Provides API for converting
// to cobalt FIDL types.
//
// This class is moveable and move-assignable.
// This class is not copy or copy-assignable.
// This class is thread-safe.
class RemoteCounter : public BaseCounter {
public:
    // Callback to notify that Flush has been completed, and that the observation buffer is
    // writeable again(this is buffer where the counter and its metadata are flushed).
    using FlushCompleteFn = fbl::Function<void()>;

    // Alias for the specific buffer instantiation.
    using EventBuffer = internal::EventBuffer<uint32_t>;

    // Function in charge persisting or processing the ObservationValue buffer.
    using FlushFn = fbl::Function<void(const RemoteMetricInfo& metric_info, const EventBuffer&,
                                       FlushCompleteFn complete)>;

    RemoteCounter() = delete;
    RemoteCounter(const RemoteMetricInfo& metric_info, EventBuffer buffer);
    RemoteCounter(const RemoteCounter&) = delete;
    RemoteCounter(RemoteCounter&&);
    RemoteCounter& operator=(const RemoteCounter&) = delete;
    RemoteCounter& operator=(RemoteCounter&&) = delete;
    ~RemoteCounter() = default;

    // Returns true if the contests were flushed.
    bool Flush(const FlushFn& flush_handler);

    // Returns the metric_id associated with this remote metric.
    const RemoteMetricInfo& metric_info() const { return metric_info_; }

private:
    // The buffer containing the data to be flushed.
    EventBuffer buffer_;

    // Unique-Id representing this metric in the backend.
    RemoteMetricInfo metric_info_;
};

} // namespace internal
} // namespace cobalt_client
