// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Test wrappers

#define TRY(func) ({\
    int ret = (func); \
    if (ret < 0) { \
        printf("%s:%d:error: %s -> %d (errno %d)\n", __FILE__, __LINE__, #func, ret, errno); \
        exit(1); \
    } \
    ret; })

#define EXPECT_FAIL(func) ({\
    int ret = (func); \
    if (ret >= 0) { \
        printf("%s:%d:expected error from: %s -> %d\n", __FILE__, __LINE__, #func, ret); \
        exit(1); \
    } \
    ret; })

// FNV-1a Hash
//
// http://www.isthe.com/chongo/tech/comp/fnv/index.html

#define FNV32_PRIME (16777619)
#define FNV32_OFFSET_BASIS (2166136261)

static inline uint32_t fnv1a32(const void* ptr, size_t len) {
    uint32_t n = FNV32_OFFSET_BASIS;
    const uint8_t* data = ptr;
    while (len-- > 0) {
        n = (n ^ (*data++)) * FNV32_PRIME;
    }
    return n;
}

#define FNV64_PRIME (1099511628211ULL)
#define FNV64_OFFSET_BASIS (14695981039346656037ULL)

static inline uint64_t fnv1a64(const void* ptr, size_t len) {
    uint64_t n = FNV64_OFFSET_BASIS;
    const uint8_t* data = ptr;
    while (len-- > 0) {
        n = (n ^ (*data++)) * FNV64_PRIME;
    }
    return n;
}

// for bits 0..15
static inline uint32_t fnv1a_tiny(uint32_t n, uint32_t bits) {
    uint32_t hash = FNV32_OFFSET_BASIS;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ (n & 0xFF)) * FNV32_PRIME; n >>= 8;
    hash = (hash ^ n) * FNV32_PRIME;
    return ((hash >> bits) ^ hash) & ((1 << bits) - 1);
}

#define fnv1a32str(str) fnv1a32(str, strlen(str))
#define fnv1a64str(str) fnv1a64(str, strlen(str))


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
