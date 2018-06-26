// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <cobalt-client/cpp/observation.h>
#include <fbl/atomic.h>
#include <fbl/string.h>

namespace cobalt_client {

// Wrapper class which fixates the memory ordering for the underlying atomic
// to |Counter::MemoryOrder|.
//
// This type is not copyable, moveable or assignable.
class Counter {
public:
    using Type = uint64_t;

    // All atomic operations use this memory order.
    static constexpr fbl::memory_order kMemoryOrder = fbl::memory_order::memory_order_relaxed;

    Counter() = delete;
    // For constructing a counter that represents a Value.
    Counter(uint64_t metric_id, uint32_t encoding_id)
        : name_(), counter_(0), metric_id_(metric_id), encoding_id_(encoding_id) {}
    // For constructing a counter that represents an ObservationValue..
    Counter(const fbl::String& name, uint64_t metric_id, uint32_t encoding_id)
        : name_(name), counter_(0), metric_id_(metric_id), encoding_id_(encoding_id) {}
    Counter(const Counter&) = delete;
    Counter(Counter&&);
    Counter& operator=(const Counter&) = delete;
    Counter& operator=(Counter&&) = delete;
    ~Counter() = default;

    // Increments |counter_| by val.
    void Increment(Type val = 1) { counter_.fetch_add(val, kMemoryOrder); }

    // Returns the current value of|counter_| and resets it to |val|.
    Type Exchange(Type val = 0) { return counter_.exchange(val, kMemoryOrder); }

    // Returns the current value of |counter_|.
    Type Load() const { return counter_.load(kMemoryOrder); }

    // Returns an ObservationValue containing the counter representation.
    ObservationValue GetObservationValue() const;

    // Returns an ObservationValue containing the counter representation, and resets the value of
    // |counter_| to |val|.
    ObservationValue GetObservationValueAndExchange(Type val = 0);

    uint64_t metric_id() const { return metric_id_; }

private:
    fbl::String name_;
    fbl::atomic<Type> counter_;
    uint64_t metric_id_;
    uint32_t encoding_id_;
};

} // namespace cobalt_client
