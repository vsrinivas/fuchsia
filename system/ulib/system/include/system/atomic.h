// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <system/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

/* Strongly ordered, and then relaxed, versions of the atomic routines
 * as implemented by the compiler with arch-dependent memory
 * barriers. Defined for with a short name for int, and with longer
 * names for sized integers.
 */
#define __MAKE_ATOMICS(TYPE, TYPE_NAME)                                                         \
    static inline TYPE atomic_swap##TYPE_NAME(TYPE* ptr, TYPE val) {                            \
        return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);                                 \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_add##TYPE_NAME(TYPE* ptr, TYPE val) {                             \
        return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);                                  \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_and##TYPE_NAME(TYPE* ptr, TYPE val) {                             \
        return __atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST);                                  \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_or##TYPE_NAME(TYPE* ptr, TYPE val) {                              \
        return __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST);                                   \
    }                                                                                           \
                                                                                                \
    static inline bool atomic_cmpxchg##TYPE_NAME(TYPE* ptr, TYPE* oldval, TYPE newval) {        \
        return __atomic_compare_exchange_n(ptr, oldval, newval, 0,                              \
                                           __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);                 \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_load##TYPE_NAME(TYPE* ptr) {                                      \
        return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);                                          \
    }                                                                                           \
                                                                                                \
    static inline void atomic_store##TYPE_NAME(TYPE* ptr, TYPE newval) {                        \
        __atomic_store_n(ptr, newval, __ATOMIC_SEQ_CST);                                        \
    }                                                                                           \
                                                                                                \
    /* relaxed versions of the above */                                                         \
    static inline TYPE atomic_swap_relaxed##TYPE_NAME(TYPE* ptr, TYPE val) {                    \
        return __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED);                                 \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_add_relaxed##TYPE_NAME(TYPE* ptr, TYPE val) {                     \
        return __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED);                                  \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_and_relaxed##TYPE_NAME(TYPE* ptr, TYPE val) {                     \
        return __atomic_fetch_and(ptr, val, __ATOMIC_RELAXED);                                  \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_or_relaxed##TYPE_NAME(TYPE* ptr, TYPE val) {                      \
        return __atomic_fetch_or(ptr, val, __ATOMIC_RELAXED);                                   \
    }                                                                                           \
                                                                                                \
    static inline TYPE atomic_cmpxchg_relaxed##TYPE_NAME(TYPE* ptr, TYPE oldval, TYPE newval) { \
        (void)__atomic_compare_exchange_n(ptr, &oldval, newval, 0,                              \
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED);                  \
        return oldval;                                                                          \
    }                                                                                           \
                                                                                                \
    static TYPE atomic_load_relaxed##TYPE_NAME(TYPE* ptr) {                                     \
        return __atomic_load_n(ptr, __ATOMIC_RELAXED);                                          \
    }                                                                                           \
                                                                                                \
    static void atomic_store_relaxed##TYPE_NAME(TYPE* ptr, TYPE newval) {                       \
        __atomic_store_n(ptr, newval, __ATOMIC_RELAXED);                                        \
    }

__MAKE_ATOMICS(uint8_t, _uint8)
__MAKE_ATOMICS(uint16_t, _uint16)
__MAKE_ATOMICS(uint32_t, _uint32)
__MAKE_ATOMICS(uint64_t, _uint64)
__MAKE_ATOMICS(int8_t, _int8)
__MAKE_ATOMICS(int16_t, _int16)
__MAKE_ATOMICS(int32_t, _int32)
__MAKE_ATOMICS(int64_t, _int64)
__MAKE_ATOMICS(int, )
__MAKE_ATOMICS(bool, _bool)

__END_CDECLS
