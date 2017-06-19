// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/type_support.h>

#include <stddef.h>
#include <stdint.h>

// mxtl::atomic<T> provides typesafe C++ atomics on integral
// types. It does not support:
// - bool, as the desired interface is rather different
// - pointer types, though they could be easily added
// - wide characters

// The interface closely matches the underlying builtins and the
// standard C and C++ interfaces. Member function and nonmember
// function versions of operations are provided. No operator overloads
// for e.g. += are provided.

// Only the compare-exchange overloads that require both memory orders
// explicitly are provided. The rules around what values to use for
// the success and failure cases in the single order overload are
// subtle. We similarly don't have _explicit vs. not variants of
// things, as the std:: versions do.

namespace mxtl {

// The underlying builtins specify the memory order parameters as an
// int, so let's be explicit here.
enum memory_order : int {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST,
};

template <typename T>
struct atomic {
    static_assert(is_integral<T>::value, "mxtl::atomic only support integral types");
    static_assert(!is_same<T, bool>::value, "mxtl::atomic does not support bool");
    static_assert(!is_same<T, wchar_t>::value, "mxtl::atomic does not support wide characters");
    static_assert(!is_same<T, char16_t>::value, "mxtl::atomic does not support wide characters");
    static_assert(!is_same<T, char32_t>::value, "mxtl::atomic does not support wide characters");
    static_assert(__atomic_always_lock_free(sizeof(T), nullptr),
                  "The requested integer size is not statically guaranteed to be atomically modifiable");

    // The default constructor does not initialize the value! This is
    // the same as plain old integer types.
    atomic() = default;
    constexpr atomic(T value)
        : value_(value) {}

    // Don't copy, move, or operator= atomic values. Use store instead
    // of operator=.
    atomic(const atomic& value) = delete;
    atomic(atomic&& value) = delete;
    void operator=(atomic value) = delete;
    void operator=(atomic value) volatile = delete;
    atomic& operator=(const atomic& value) = delete;
    atomic& operator=(const atomic& value) volatile = delete;
    atomic& operator=(atomic&& value) = delete;
    atomic& operator=(atomic&& value) volatile = delete;

    void store(T value, memory_order order = memory_order_seq_cst) {
        __atomic_store_n(&value_, value, order);
    };
    void store(T value, memory_order order = memory_order_seq_cst) volatile {
        __atomic_store_n(&value_, value, order);
    }

    T load(memory_order order = memory_order_seq_cst) const {
        return __atomic_load_n(&value_, order);
    }
    T load(memory_order order = memory_order_seq_cst) const volatile {
        return __atomic_load_n(&value_, order);
    }

    T exchange(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_exchange_n(&value_, value, order);
    }
    T exchange(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_exchange_n(&value_, value, order);
    }

    // Note that the std:: versions take a mutable _reference_ to
    // expected, not a pointer. In addition, it provides overloads like
    //    compare_exchange_weak(T* expected, T desired,
    //                          memory_order order = memory_order_seq_cst);
    // which are rather magic in that the release orders imply
    // different success and failure orders, which feels nonobvious
    // enough to not provide them.
    bool compare_exchange_weak(T* expected, T desired,
                               memory_order success_order,
                               memory_order failure_order) {
        return __atomic_compare_exchange_n(&value_, expected, desired, /* weak */ true,
                                           success_order, failure_order);
    }
    bool compare_exchange_weak(T* expected, T desired,
                               memory_order success_order,
                               memory_order failure_order) volatile {
        return __atomic_compare_exchange_n(&value_, expected, desired, /* weak */ true,
                                           success_order, failure_order);
    }

    bool compare_exchange_strong(T* expected, T desired,
                                 memory_order success_order,
                                 memory_order failure_order) {
        return __atomic_compare_exchange_n(&value_, expected, desired, /* weak */ false,
                                           success_order, failure_order);
    }
    bool compare_exchange_strong(T* expected, T desired,
                                 memory_order success_order,
                                 memory_order failure_order) volatile {
        return __atomic_compare_exchange_n(&value_, expected, desired, /* weak */ false,
                                           success_order, failure_order);
    }

    T fetch_add(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_fetch_add(&value_, value, order);
    }
    T fetch_add(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_fetch_add(&value_, value, order);
    }

    T fetch_sub(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_fetch_sub(&value_, value, order);
    }
    T fetch_sub(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_fetch_sub(&value_, value, order);
    }

    T fetch_and(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_fetch_and(&value_, value, order);
    }
    T fetch_and(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_fetch_and(&value_, value, order);
    }

    T fetch_or(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_fetch_or(&value_, value, order);
    }
    T fetch_or(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_fetch_or(&value_, value, order);
    }

    T fetch_xor(T value, memory_order order = memory_order_seq_cst) {
        return __atomic_fetch_xor(&value_, value, order);
    }
    T fetch_xor(T value, memory_order order = memory_order_seq_cst) volatile {
        return __atomic_fetch_xor(&value_, value, order);
    }

private:
    T value_;
};

// Non-member function versions.
template <typename T>
void atomic_store(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    atomic_ptr->store(value, order);
}
template <typename T>
void atomic_store(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    atomic_ptr->store(value, order);
}

template <typename T>
T atomic_load(const atomic<T>* atomic_ptr, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->load(order);
}
template <typename T>
T atomic_load(const volatile atomic<T>* atomic_ptr, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->load(order);
}

template <typename T>
T atomic_exchange(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->exchange(value, order);
}
template <typename T>
T atomic_exchange(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->exchange(value, order);
}

template <typename T>
bool atomic_compare_exchange_weak(atomic<T>* atomic_ptr, T* expected, T desired,
                                  memory_order success_order,
                                  memory_order failure_order) {
    return atomic_ptr->compare_exchange_weak(expected, desired, success_order, failure_order);
}
template <typename T>
bool atomic_compare_exchange_weak(volatile atomic<T>* atomic_ptr, T* expected, T desired,
                                  memory_order success_order,
                                  memory_order failure_order) {
    return atomic_ptr->compare_exchange_weak(expected, desired, success_order, failure_order);
}

template <typename T>
bool atomic_compare_exchange_strong(atomic<T>* atomic_ptr, T* expected, T desired,
                                    memory_order success_order,
                                    memory_order failure_order) {
    return atomic_ptr->compare_exchange_strong(expected, desired, success_order, failure_order);
}

template <typename T>
bool atomic_compare_exchange_strong(volatile atomic<T>* atomic_ptr, T* expected, T desired,
                                    memory_order success_order,
                                    memory_order failure_order) {
    return atomic_ptr->compare_exchange_strong(expected, desired, success_order, failure_order);
}

template <typename T>
T atomic_fetch_add(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_add(value, order);
}
template <typename T>
T atomic_fetch_add(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_add(value, order);
}

template <typename T>
T atomic_fetch_sub(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_sub(value, order);
}
template <typename T>
T atomic_fetch_sub(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_sub(value, order);
}

template <typename T>
T atomic_fetch_and(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_and(value, order);
}
template <typename T>
T atomic_fetch_and(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_and(value, order);
}

template <typename T>
T atomic_fetch_or(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_or(value, order);
}
template <typename T>
T atomic_fetch_or(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_or(value, order);
}

template <typename T>
T atomic_fetch_xor(atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_xor(value, order);
}
template <typename T>
T atomic_fetch_xor(volatile atomic<T>* atomic_ptr, T value, memory_order order = memory_order_seq_cst) {
    return atomic_ptr->fetch_xor(value, order);
}

// Other atomic functions.
template <typename T>
void atomic_init(atomic<T>* atomic_ptr, T value) {
    atomic_ptr->value_ = value;
}
template <typename T>
void atomic_init(volatile atomic<T>* atomic_ptr, T value) {
    atomic_ptr->value_ = value;
}

inline void atomic_thread_fence(memory_order order = memory_order_seq_cst) {
    __atomic_thread_fence(order);
}

inline void atomic_signal_fence(memory_order order = memory_order_seq_cst) {
    __atomic_signal_fence(order);
}

// Aliases for all integer type names.
using atomic_char = atomic<char>;
using atomic_schar = atomic<signed char>;
using atomic_uchar = atomic<unsigned char>;
using atomic_short = atomic<short>;
using atomic_ushort = atomic<unsigned short>;
using atomic_int = atomic<int>;
using atomic_uint = atomic<unsigned int>;
using atomic_long = atomic<long>;
using atomic_ulong = atomic<unsigned long>;
using atomic_llong = atomic<long long>;
using atomic_ullong = atomic<unsigned long long>;

using atomic_intptr_t = atomic<intptr_t>;
using atomic_uintptr_t = atomic<uintptr_t>;
using atomic_size_t = atomic<size_t>;
using atomic_ptrdiff_t = atomic<ptrdiff_t>;
using atomic_intmax_t = atomic<intmax_t>;
using atomic_uintmax_t = atomic<uintmax_t>;

using atomic_int8_t = atomic<int8_t>;
using atomic_uint8_t = atomic<uint8_t>;
using atomic_int16_t = atomic<int16_t>;
using atomic_uint16_t = atomic<uint16_t>;
using atomic_int32_t = atomic<int32_t>;
using atomic_uint32_t = atomic<uint32_t>;
using atomic_int64_t = atomic<int64_t>;
using atomic_uint64_t = atomic<uint64_t>;

using atomic_int_least8_t = atomic<int_least8_t>;
using atomic_uint_least8_t = atomic<uint_least8_t>;
using atomic_int_least16_t = atomic<int_least16_t>;
using atomic_uint_least16_t = atomic<uint_least16_t>;
using atomic_int_least32_t = atomic<int_least32_t>;
using atomic_uint_least32_t = atomic<uint_least32_t>;
using atomic_int_least64_t = atomic<int_least64_t>;
using atomic_uint_least64_t = atomic<uint_least64_t>;
using atomic_int_fast8_t = atomic<int_fast8_t>;
using atomic_uint_fast8_t = atomic<uint_fast8_t>;
using atomic_int_fast16_t = atomic<int_fast16_t>;
using atomic_uint_fast16_t = atomic<uint_fast16_t>;
using atomic_int_fast32_t = atomic<int_fast32_t>;
using atomic_uint_fast32_t = atomic<uint_fast32_t>;
using atomic_int_fast64_t = atomic<int_fast64_t>;
using atomic_uint_fast64_t = atomic<uint_fast64_t>;

} // namespace mxtl
