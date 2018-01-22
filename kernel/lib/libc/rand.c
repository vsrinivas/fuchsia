// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <rand.h>

#include <kernel/atomic.h>
#include <sys/types.h>

static unsigned int randseed = 12345;

void srand(unsigned int seed)
{
    atomic_store_relaxed((int*)&randseed, seed);
}

void rand_add_entropy(const void *buf, size_t len)
{
    if (len == 0)
        return;

    uint32_t enp = 0;
    for (size_t i = 0; i < len; i++) {
        enp ^= ((enp << 8) | (enp >> 24)) ^ ((const uint8_t *)buf)[i];
    }

    atomic_xor_relaxed((int*)&randseed, enp);
}

int rand(void)
{
    for (;;) {
        int old_seed = atomic_load((int*)&randseed);
        int new_seed = old_seed * 1664525 + 1013904223;
        if (atomic_cmpxchg_relaxed((int*)&randseed, &old_seed, new_seed))
            return new_seed;
    }
}
