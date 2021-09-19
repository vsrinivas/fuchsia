// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ATOMIC_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ATOMIC_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <type_traits>

#include "../memory.h"
#include "../type_traits.h"

namespace cpp20 {
namespace atomic_internal {

// Maps |std::memory_order| to builtin |__ATOMIC_XXXX| values.
constexpr int to_builtin_memory_order(std::memory_order order) {
  switch (order) {
    case std::memory_order_relaxed:
      return __ATOMIC_RELAXED;
    case std::memory_order_consume:
      return __ATOMIC_CONSUME;
    case std::memory_order_acquire:
      return __ATOMIC_ACQUIRE;
    case std::memory_order_release:
      return __ATOMIC_RELEASE;
    case std::memory_order_acq_rel:
      return __ATOMIC_ACQ_REL;
    case std::memory_order_seq_cst:
      return __ATOMIC_SEQ_CST;
    default:
      __builtin_abort();
  }
}

// Applies corrections to |order| on |compare_exchange|'s load operations.
constexpr std::memory_order compare_exchange_load_memory_order(std::memory_order order) {
  if (order == std::memory_order_acq_rel) {
    return std::memory_order_acquire;
  }

  if (order == std::memory_order_release) {
    return std::memory_order_relaxed;
  }

  return order;
}

// Unspecialized types alignment, who have a size that matches a known integer(power of 2 bytes)
// should be aligned to its size at least for better performance. Otherwise, we default to its
// default alignment.
template <typename T, typename Enabled = void>
struct alignment {
  static constexpr size_t required_alignment =
      std::max((sizeof(T) & (sizeof(T) - 1) || sizeof(T) > 16) ? 0 : sizeof(T), alignof(T));
};

template <typename T>
struct alignment<T, std::enable_if_t<cpp17::is_integral_v<T>>> {
  static constexpr size_t required_alignment = sizeof(T) > alignof(T) ? sizeof(T) : alignof(T);
};

template <typename T>
struct alignment<T, std::enable_if_t<cpp17::is_pointer_v<T> || cpp17::is_floating_point_v<T>>> {
  static constexpr size_t required_alignment = alignof(T);
};

template <typename T>
static constexpr bool unqualified = cpp17::is_same_v<T, std::remove_cv_t<T>>;

// Provide atomic operations based on compiler builtins.
template <typename Derived, typename T>
class atomic_ops {
 private:
  // Removes |volatile| and deprecation messages from static analizers.
  using value_t = std::remove_cv_t<T>;

  // Storage.
  using storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

 public:
  void store(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    __atomic_store(ptr(), cpp17::addressof(desired), to_builtin_memory_order(order));
  }

  T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    storage_t store;
    value_t* ret = reinterpret_cast<value_t*>(&store);
    __atomic_load(ptr(), ret, to_builtin_memory_order(order));
    return *ret;
  }

  operator T() const noexcept { return this->load(); }

  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    storage_t store;
    value_t* ret = reinterpret_cast<value_t*>(&store);
    value_t noncv_desired = desired;
    __atomic_exchange(ptr(), cpp17::addressof(noncv_desired), ret, to_builtin_memory_order(order));
    return *ret;
  }

  bool compare_exchange_weak(T& expected, T desired,
                             std::memory_order success = std::memory_order_seq_cst) const noexcept {
    return compare_exchange_weak(expected, desired, success,
                                 compare_exchange_load_memory_order(success));
  }

  bool compare_exchange_weak(T& expected, T desired, std::memory_order success,
                             std::memory_order failure) const noexcept {
    check_failure_memory_order(failure);
    return __atomic_compare_exchange(ptr(), cpp17::addressof(expected), cpp17::addressof(desired),
                                     /*weak=*/true, to_builtin_memory_order(success),
                                     to_builtin_memory_order(failure));
  }

  bool compare_exchange_strong(
      T& expected, T desired,
      std::memory_order success = std::memory_order_seq_cst) const noexcept {
    return compare_exchange_strong(expected, desired, success,
                                   compare_exchange_load_memory_order(success));
  }

  bool compare_exchange_strong(T& expected, T desired, std::memory_order success,
                               std::memory_order failure) const noexcept {
    check_failure_memory_order(failure);
    return __atomic_compare_exchange(ptr(), cpp17::addressof(expected), cpp17::addressof(desired),
                                     /*weak=*/false, to_builtin_memory_order(success),
                                     to_builtin_memory_order(failure));
  }

 private:
  // |failure| memory order may not be |std::memory_order_release| or
  // |std::memory_order_acq_release|.
  constexpr void check_failure_memory_order(std::memory_order failure) const {
    if (failure == std::memory_order_acq_rel || failure == std::memory_order_release) {
      __builtin_abort();
    }
  }
  constexpr T* ptr() const { return static_cast<const Derived*>(this)->ptr_; }
};

// Delegate to helper templates the arguments which differ between pointer and integral types.
template <typename T>
struct arithmetic_ops_helper {
  // Return type of |ptr| method.
  using ptr_type = T*;

  // Return type of atomic builtins.
  using return_type = T;

  // Type of operands used.
  using operand_type = T;

  // Arithmetic operands are amplified by this scalar.
  static constexpr size_t modifier = 1;
};

template <typename T>
struct arithmetic_ops_helper<T*> {
  // Return type of |ptr| method.
  using ptr_type = T**;

  // Return type of atomic builtins.
  using return_type = T*;

  // Type of operands used.
  using operand_type = ptrdiff_t;

  // Arithmetic operands are amplified by this scalar.
  static constexpr size_t modifier = sizeof(T);
};

template <typename T>
using difference_t = typename arithmetic_ops_helper<T>::operand_type;

// Arithmetic operations.
//
// Enables :
//  - fetch_add
//  - fetch_sub
//  - operator++
//  - operator--
//  - operator+=
//  - operator-=
template <typename Derived, typename T, typename Enabled = void>
class arithmetic_ops {};

// Pointer and Integral operations.
template <typename Derived, typename T>
class arithmetic_ops<
    Derived, T,
    std::enable_if_t<(cpp17::is_integral_v<T> && unqualified<T> && !cpp17::is_same_v<T, bool>) ||
                     cpp17::is_pointer_v<T>>> {
  using return_t = typename arithmetic_ops_helper<T>::return_type;
  using operand_t = typename arithmetic_ops_helper<T>::operand_type;
  using ptr_t = typename arithmetic_ops_helper<T>::ptr_type;
  static constexpr auto modifier = arithmetic_ops_helper<T>::modifier;

 public:
  return_t fetch_add(operand_t operand,
                     std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_add(ptr(), operand * modifier, to_builtin_memory_order(order));
  }

  return_t fetch_sub(operand_t operand,
                     std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_sub(ptr(), operand * modifier, to_builtin_memory_order(order));
  }

  return_t operator++(int) const noexcept { return fetch_add(1); }
  return_t operator--(int) const noexcept { return fetch_sub(1); }
  return_t operator++() const noexcept { return fetch_add(1) + 1; }
  return_t operator--() const noexcept { return fetch_sub(1) - 1; }
  return_t operator+=(operand_t operand) const noexcept { return fetch_add(operand) + operand; }
  return_t operator-=(operand_t operand) const noexcept { return fetch_sub(operand) - operand; }

 private:
  constexpr ptr_t ptr() const { return static_cast<const Derived*>(this)->ptr_; }
};

// Floating point arithmetic operations.
// Based on CAS cycles to perform atomic add and sub.
template <typename Derived, typename T>
class arithmetic_ops<Derived, T,
                     std::enable_if_t<cpp17::is_floating_point_v<T> && unqualified<T>>> {
 public:
  T fetch_add(T operand, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    T old_value = derived()->load(std::memory_order_relaxed);
    T new_value = old_value + operand;
    while (
        !derived()->compare_exchange_weak(old_value, new_value, order, std::memory_order_relaxed)) {
      new_value = old_value + operand;
    }
    return old_value;
  }

  T fetch_sub(T operand, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    T old_value = derived()->load(std::memory_order_relaxed);
    T new_value = old_value - operand;
    while (
        !derived()->compare_exchange_weak(old_value, new_value, order, std::memory_order_relaxed)) {
      new_value = old_value - operand;
    }
    return old_value;
  }

  T operator+=(T operand) const noexcept { return fetch_add(operand) + operand; }
  T operator-=(T operand) const noexcept { return fetch_sub(operand) - operand; }

 private:
  constexpr T ptr() const { return static_cast<const Derived*>(this)->ptr_; }
  constexpr const Derived* derived() const { return static_cast<const Derived*>(this); }
};

// Bitwise operations.
//
// Enables :
//  - fetch_and
//  - fetch_or
//  - fetch_xor
//  - operator&=
//  - operator|=
//  - operator^=
template <typename Derived, typename T, typename Enabled = void>
class bitwise_ops {};

template <typename Derived, typename T>
class bitwise_ops<
    Derived, T,
    std::enable_if_t<cpp17::is_integral_v<T> && unqualified<T> && !cpp17::is_same_v<T, bool>>> {
 public:
  T fetch_and(T operand, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_and(ptr(), operand, to_builtin_memory_order(order));
  }

  T fetch_or(T operand, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_or(ptr(), operand, to_builtin_memory_order(order));
  }

  T fetch_xor(T operand, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_fetch_xor(ptr(), operand, to_builtin_memory_order(order));
  }

  T operator&=(T operand) const noexcept { return fetch_and(operand) & operand; }
  T operator|=(T operand) const noexcept { return fetch_or(operand) | operand; }
  T operator^=(T operand) const noexcept { return fetch_xor(operand) ^ operand; }

 private:
  constexpr T* ptr() const { return static_cast<const Derived*>(this)->ptr_; }
};

}  // namespace atomic_internal
}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ATOMIC_H_
