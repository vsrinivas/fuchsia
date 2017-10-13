// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <stdint.h>

// types and routines for dealing with lists of cpus and cpu masks

typedef uint32_t cpu_mask_t;
typedef uint32_t cpu_num_t;

static_assert(SMP_MAX_CPUS <= sizeof(cpu_mask_t) * CHAR_BIT, "");

#define INVALID_CPU ((cpu_num_t)-1)
#define CPU_MASK_ALL ((cpu_mask_t)-1)

static inline bool is_valid_cpu_num(cpu_num_t num) {
    return (num < SMP_MAX_CPUS);
}

static inline cpu_mask_t cpu_num_to_mask(cpu_num_t num) {
    if (!is_valid_cpu_num(num))
        return 0;

    return ((cpu_mask_t)1u << num);
}

static inline cpu_num_t highest_cpu_set(cpu_mask_t mask) {
    if (mask == 0)
        return 0;

    return (cpu_num_t)(sizeof(cpu_mask_t) * CHAR_BIT - 1) - __builtin_clz(mask);
}

static inline cpu_num_t lowest_cpu_set(cpu_mask_t mask) {
    if (mask == 0)
        return 0;

    return (cpu_num_t)(__builtin_ctz(mask));
}
