// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>

#include <ktl/atomic.h>

namespace {

uint64_t TrivialPrng(uint64_t old_seed) { return (6364136223846793005ULL * old_seed) + 1; }

int TrivialRand(uint64_t prng) { return static_cast<int>(prng >> 33); }

ktl::atomic<uint64_t> gPrng;

}  // namespace

void srand(unsigned int seed) { gPrng.store(seed - 1, ktl::memory_order::relaxed); }

// rand_r doesn't need to access the state atomically.
int rand_r(uint64_t* seed) {
  *seed = TrivialPrng(*seed);
  return TrivialRand(*seed);
}

int rand() {
  uint64_t new_seed, old_seed = gPrng.load(ktl::memory_order::relaxed);
  do {
    new_seed = TrivialPrng(old_seed);
  } while (!gPrng.compare_exchange_weak(old_seed, new_seed, ktl::memory_order::relaxed));
  return TrivialRand(new_seed);
}
