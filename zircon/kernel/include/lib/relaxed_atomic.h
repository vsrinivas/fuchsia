// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_
#define ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_

#include <ktl/atomic.h>
#include <ktl/forward.h>

// A wrapper of std/ktl atomic that defaults to relaxed operations.
// Currently doesn't expose all functionality, feel free to expand as needed.
// Be careful adding read/modify/write, think carefully if they make sense as
// relaxed operations.
template<typename T>
class RelaxedAtomic {
 public:
  RelaxedAtomic() noexcept = default;
  RelaxedAtomic(T&& desired) : wrapped_(ktl::forward<T>(desired)) {}

  template<typename U>
  T operator=(U&& desired) noexcept {
    wrapped_.store(ktl::forward<U>(desired), ktl::memory_order_relaxed);
    return desired;
  }

  operator T() const noexcept {
    return load();
  }

  T load() const {
    return wrapped_.load(ktl::memory_order_relaxed);
  }

  template<typename U>
  void store(U&& value) {
    wrapped_.store(ktl::forward<U>(value), ktl::memory_order_relaxed);
  }

 private:
  ktl::atomic<T> wrapped_;
};


#endif  // ZIRCON_KERNEL_INCLUDE_LIB_RELAXED_ATOMIC_H_

