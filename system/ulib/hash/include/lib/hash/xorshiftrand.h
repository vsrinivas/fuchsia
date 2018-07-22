// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <lib/hash/hash.h>

// Xorshift32 and Xorshift64
//
// https://www.jstatsoft.org/article/view/v008i14
// https://en.wikipedia.org/wiki/Xorshift

#define RAND32SEED(n) {(n)}
#define RAND63SEED(n) {(n)}

typedef struct {
    uint32_t n;
} rand32_t;

typedef struct {
    uint64_t n;
} rand64_t;

static inline uint32_t rand32(rand32_t* state) {
    uint32_t n = state->n;
    n ^= (n << 13);
    n ^= (n >> 17);
    n ^= (n << 5);
    return (state->n = n);
}

static inline uint64_t rand64(rand64_t* state) {
    uint64_t n = state->n;
    n ^= (n << 13);
    n ^= (n >> 7);
    n ^= (n << 17);
    return (state->n = n);
}

static inline void srand32(rand32_t* state, const char* str) {
    state->n = fnv1a32str(str);
}

static inline void srand64(rand64_t* state, const char* str) {
    state->n = fnv1a64str(str);
}
