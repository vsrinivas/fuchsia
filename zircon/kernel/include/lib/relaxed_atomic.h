// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_
#define ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_

#include <ktl/atomic.h>

// Wrapper around ktl::atomic that assumes ktl::memory_order_relaxed for all
// operations to simplify pure relaxed use cases. Only a subset of operations
// are supported as needed.
//
// NOTE: ktl::atomic has specific defintions for implicit constructors,
// coversion operators, and compound assignment operators. This utility attempts
// to mirror the signatures and beahvior of the underlying atomic as closely as
// possible.
template <typename T>
class RelaxedAtomic {
 public:
  RelaxedAtomic() noexcept = default;
  constexpr RelaxedAtomic(T desired) : wrapped_(desired) {}

  T load() const noexcept { return wrapped_.load(ktl::memory_order_relaxed); }
  void store(T desired) noexcept { wrapped_.store(desired, ktl::memory_order_relaxed); }
  T fetch_add(T value) noexcept { return wrapped_.fetch_add(value, ktl::memory_order_relaxed); }

  operator T() const noexcept { return load(); }

  T operator=(T desired) noexcept { return store(desired), desired; }
  T operator+=(T value) noexcept { return fetch_add(value) + value; }

 private:
  ktl::atomic<T> wrapped_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_
