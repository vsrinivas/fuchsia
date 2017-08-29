// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Internal declarations used by the C tracing macros.
// This is not part of the public API: use <trace/event.h> instead.
//

#pragma once

// Count the number of pairs of arguments passed to it without evaluating them.
// When the number of arguments is uneven, rounds down.
// Works with 0 to 15 pairs.
#define TRACE_INTERNAL_COUNT_PAIRS(...)                     \
    TRACE_INTERNAL_COUNT_PAIRS_(__VA_ARGS__,                \
                                15, 14, 14, 13, 13, 12, 12, \
                                11, 11, 10, 10, 9, 9, 8, 8, \
                                7, 7, 6, 6, 5, 5, 4, 4,     \
                                3, 3, 2, 2, 1, 1, 0, 0)
#define TRACE_INTERNAL_COUNT_PAIRS_(            \
    _15, _15X, _14, _14X, _13, _13X, _12, _12X, \
    _11, _11X, _10, _10X, _9, _9X, _8, _8X,     \
    _7, _7X, _6, _6X, _5, _5X, _4, _4X,         \
    _3, _3X, _2, _2X, _1, _1X, N, ...) N

// Applies a function or macro to each pair of arguments to produce a
// comma-separated result.  Works with 0 to 15 pairs.
#define TRACE_INTERNAL_APPLY_PAIRWISE(fn, ...)                              \
    TRACE_INTERNAL_APPLY_PAIRWISE_(TRACE_INTERNAL_COUNT_PAIRS(__VA_ARGS__)) \
    (fn, __VA_ARGS__)
#define TRACE_INTERNAL_APPLY_PAIRWISE_(n) TRACE_INTERNAL_APPLY_PAIRWISE__(n)
#define TRACE_INTERNAL_APPLY_PAIRWISE__(n) TRACE_INTERNAL_APPLY_PAIRWISE##n
#define TRACE_INTERNAL_APPLY_PAIRWISE0(fn, ...)
#define TRACE_INTERNAL_APPLY_PAIRWISE1(fn, k1, v1) \
    (fn(k1, v1))
#define TRACE_INTERNAL_APPLY_PAIRWISE2(fn, k1, v1, k2, v2) \
    (fn(k1, v1)), (fn(k2, v2))
#define TRACE_INTERNAL_APPLY_PAIRWISE3(fn, k1, v1, k2, v2, k3, v3) \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3))
#define TRACE_INTERNAL_APPLY_PAIRWISE4(fn, k1, v1, k2, v2, k3, v3, k4, v4) \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4))
#define TRACE_INTERNAL_APPLY_PAIRWISE5(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                       k5, v5)                             \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                \
        (fn(k5, v5))
#define TRACE_INTERNAL_APPLY_PAIRWISE6(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                       k5, v5, k6, v6)                     \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                \
        (fn(k5, v5)), (fn(k6, v6))
#define TRACE_INTERNAL_APPLY_PAIRWISE7(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                       k5, v5, k6, v6, k7, v7)             \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7))
#define TRACE_INTERNAL_APPLY_PAIRWISE8(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                       k5, v5, k6, v6, k7, v7, k8, v8)     \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8))
#define TRACE_INTERNAL_APPLY_PAIRWISE9(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                       k5, v5, k6, v6, k7, v7, k8, v8,     \
                                       k9, v9)                             \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),            \
        (fn(k9, v9))
#define TRACE_INTERNAL_APPLY_PAIRWISE10(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                        k5, v5, k6, v6, k7, v7, k8, v8,     \
                                        k9, v9, k10, v10)                   \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                 \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),             \
        (fn(k9, v9)), (fn(k10, v10))
#define TRACE_INTERNAL_APPLY_PAIRWISE11(fn, k1, v1, k2, v2, k3, v3, k4, v4, \
                                        k5, v5, k6, v6, k7, v7, k8, v8,     \
                                        k9, v9, k10, v10, k11, v11)         \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                 \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),             \
        (fn(k9, v9)), (fn(k10, v10)), (fn(k11, v11))
#define TRACE_INTERNAL_APPLY_PAIRWISE12(fn, k1, v1, k2, v2, k3, v3, k4, v4,   \
                                        k5, v5, k6, v6, k7, v7, k8, v8,       \
                                        k9, v9, k10, v10, k11, v11, k12, v12) \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                   \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),               \
        (fn(k9, v9)), (fn(k10, v10)), (fn(k11, v11)), (fn(k12, v12))
#define TRACE_INTERNAL_APPLY_PAIRWISE13(fn, k1, v1, k2, v2, k3, v3, k4, v4,   \
                                        k5, v5, k6, v6, k7, v7, k8, v8,       \
                                        k9, v9, k10, v10, k11, v11, k12, v12, \
                                        k13, v13)                             \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                   \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),               \
        (fn(k9, v9)), (fn(k10, v10)), (fn(k11, v11)), (fn(k12, v12)),         \
        (fn(k13, v13))
#define TRACE_INTERNAL_APPLY_PAIRWISE14(fn, k1, v1, k2, v2, k3, v3, k4, v4,   \
                                        k5, v5, k6, v6, k7, v7, k8, v8,       \
                                        k9, v9, k10, v10, k11, v11, k12, v12, \
                                        k13, v13, k14, v14)                   \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                   \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),               \
        (fn(k9, v9)), (fn(k10, v10)), (fn(k11, v11)), (fn(k12, v12)),         \
        (fn(k13, v13)), (fn(k14, v14))
#define TRACE_INTERNAL_APPLY_PAIRWISE15(fn, k1, v1, k2, v2, k3, v3, k4, v4,   \
                                        k5, v5, k6, v6, k7, v7, k8, v8,       \
                                        k9, v9, k10, v10, k11, v11, k12, v12, \
                                        k13, v13, k14, v14, k15, v15)         \
    (fn(k1, v1)), (fn(k2, v2)), (fn(k3, v3)), (fn(k4, v4)),                   \
        (fn(k5, v5)), (fn(k6, v6)), (fn(k7, v7)), (fn(k8, v8)),               \
        (fn(k9, v9)), (fn(k10, v10)), (fn(k11, v11)), (fn(k12, v12)),         \
        (fn(k13, v13)), (fn(k14, v14)), (fn(k15, v15))
