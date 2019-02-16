// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <utility>

namespace ktl {

template <typename T>
using atomic = std::atomic<T>;

using memory_order = std::memory_order;

inline constexpr const memory_order memory_order_relaxed = std::memory_order_relaxed;
inline constexpr const memory_order memory_order_consume = std::memory_order_consume;
inline constexpr const memory_order memory_order_acquire = std::memory_order_acquire;
inline constexpr const memory_order memory_order_release = std::memory_order_release;
inline constexpr const memory_order memory_order_acq_rel = std::memory_order_acq_rel;
inline constexpr const memory_order memory_order_seq_cst = std::memory_order_seq_cst;

template <typename T>
void atomic_init(atomic<T>* pointer, T&& value) {
    std::atomic_init(pointer, std::forward<T>(value));
}

template <typename T>
void atomic_init(volatile atomic<T>* pointer, T&& value) {
    std::atomic_init(pointer, std::forward<T>(value));
}

} // namespace ktl
