// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <platform.h>

static inline void jent_get_nstime(uint64_t* out) {
    *out = (uint64_t) current_time();
}

static inline void* jent_zalloc(size_t len) {
    return NULL;
}

static inline void jent_zfree(void* ptr, size_t len) {
}

static inline int jent_fips_enabled(void) {
    return 0;
}

static inline uint64_t rol64(uint64_t x, uint32_t n) {
    return (x<<n) | (x>>(64-n));
}
