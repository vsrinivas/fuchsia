// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <rand.h>

#include <kernel/atomic.h>
#include <sys/types.h>

static uint64_t randseed;

void srand(unsigned int seed) {
    atomic_store_u64_relaxed(&randseed, seed - 1);
}

int rand(void) {
    for (;;) {
        uint64_t old_seed = atomic_load_u64_relaxed(&randseed);
        uint64_t new_seed = 6364136223846793005ULL * old_seed + 1;
        if (atomic_cmpxchg_u64_relaxed(&randseed, &old_seed, new_seed)) {
            return new_seed >> 33;
        }
    }
}
