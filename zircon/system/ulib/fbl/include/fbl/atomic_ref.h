// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ATOMIC_REF_H_
#define FBL_ATOMIC_REF_H_

#include <type_traits>

namespace fbl {

// atomic_ref wraps an underlying object and allows atomic operations on the
// underlying object.
//
// fbl::atomic_ref is a subset of std::atomic_ref, until that is available.
// fbl::atomic_ref is only implemented for integral types at this time and
// does not implement wait() / notify_*()

// atomic_ref is useful when dealing with ABI types or when interacting with
// types that are fixed for external reasons; in all other cases, you prefer
// atomic<T>.

enum memory_order {
  memory_order_relaxed = __ATOMIC_RELAXED,
  memory_order_consume = __ATOMIC_CONSUME,
  memory_order_acquire = __ATOMIC_ACQUIRE,
  memory_order_release = __ATOMIC_RELEASE,
  memory_order_acq_rel = __ATOMIC_ACQ_REL,
  memory_order_seq_cst = __ATOMIC_SEQ_CST
};

template <typename T>
class atomic_ref {
 public:
  // atomic_ref is only implemented for integral types, which is a stronger requirement than
  // std's, which only requires T be trivially copyable.
  static_assert(std::is_integral_v<T>);
  using value_type = T;
  using difference_type = value_type;

  static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), nullptr);

  atomic_ref() = delete;
  explicit atomic_ref(T& obj) : ptr_(&obj) {}
  atomic_ref(const atomic_ref&) noexcept = default;

  T operator=(T desired) const noexcept {
    __atomic_store_n(ptr_, desired, __ATOMIC_SEQ_CST);
    return desired;
  }
  atomic_ref& operator=(const atomic_ref&) = delete;

  bool is_lock_free() const noexcept {
    // TODO(fxbug.dev/47117): Correctly implement is_lock_free based on compiler ability.
    return __atomic_always_lock_free(sizeof(T), nullptr);
  }
  void store(T desired, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    __atomic_store_n(ptr_, desired, static_cast<int>(order));
  }
  T load(fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_load_n(ptr_, static_cast<int>(order));
  }
  T exchange(T desired, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_exchange_n(ptr_, desired, static_cast<int>(order));
  }
  bool compare_exchange_weak(T& expected, T desired, fbl::memory_order success,
                             fbl::memory_order failure) const noexcept {
    return __atomic_compare_exchange_n(ptr_, &expected, desired, true,
                                       static_cast<int>(success), static_cast<int>(failure));
  }
  bool compare_exchange_weak(T& expected, T desired,
                             fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_compare_exchange_n(ptr_, &expected, desired, true,
                                       static_cast<int>(order), static_cast<int>(order));
  }
  bool compare_exchange_strong(T& expected, T desired, fbl::memory_order success,
                               fbl::memory_order failure) const noexcept {
    return __atomic_compare_exchange_n(ptr_, &expected, desired, false,
                                       static_cast<int>(success), static_cast<int>(failure));
  }
  bool compare_exchange_strong(T& expected, T desired,
                               fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_compare_exchange_n(ptr_, &expected, desired, false,
                                       static_cast<int>(order), static_cast<int>(order));
  }

  T fetch_add(T arg, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_add(ptr_, arg, static_cast<int>(order));
  }
  T fetch_sub(T arg, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_sub(ptr_, arg, static_cast<int>(order));
  }
  T fetch_and(T arg, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_and(ptr_, arg, static_cast<int>(order));
  }
  T fetch_or(T arg, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_or(ptr_, arg, static_cast<int>(order));
  }
  T fetch_xor(T arg, fbl::memory_order order = fbl::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_xor(ptr_, arg, static_cast<int>(order));
  }
  T operator++() const noexcept { return fetch_add(1) + 1; }
  T operator++(int) const noexcept { return fetch_add(1); }
  T operator--() const noexcept { return fetch_sub(1) - 1; }
  T operator--(int) const noexcept { return fetch_sub(1); }
  T operator+=(T arg) const noexcept { return fetch_add(arg) + arg; }
  T operator-=(T arg) const noexcept { return fetch_sub(arg) - arg; }
  T operator&=(T arg) const noexcept { return fetch_and(arg) & arg; }
  T operator|=(T arg) const noexcept { return fetch_or(arg) | arg; }
  T operator^=(T arg) const noexcept { return fetch_xor(arg) ^ arg; }

 private:
  T* const ptr_;
};

}  // namespace fbl

#endif  // FBL_ATOMIC_REF_H_
