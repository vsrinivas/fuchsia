// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>

#include <ktl/atomic.h>

namespace {

template <size_t = sizeof(uintptr_t)>
struct Prng;

template <>
struct Prng<4> {
  [[maybe_unused]] static constexpr uint32_t kMultiplier = 1664525;
  [[maybe_unused]] static constexpr uint32_t kIncrement = 1013904223;
  [[maybe_unused]] static constexpr int kRandShift = 0;
};

template <>
struct Prng<8> {
  [[maybe_unused]] static constexpr uint64_t kMultiplier = 6364136223846793005;
  [[maybe_unused]] static constexpr uint64_t kIncrement = 1;
  [[maybe_unused]] static constexpr int kRandShift = 33;
};

uintptr_t TrivialPrng(uintptr_t old_seed) {
  return (Prng<>::kMultiplier * old_seed) + Prng<>::kIncrement;
}

int TrivialRand(uintptr_t prng) { return static_cast<int>(prng >> Prng<>::kRandShift); }

ktl::atomic<uintptr_t> gPrng;

}  // namespace

void srand(unsigned int seed) { gPrng.store(seed - 1, ktl::memory_order::relaxed); }

// rand_r doesn't need to access the state atomically.
int rand_r(uintptr_t* seed) {
  *seed = TrivialPrng(*seed);
  return TrivialRand(*seed);
}

int rand() {
  uintptr_t new_seed, old_seed = gPrng.load(ktl::memory_order::relaxed);
  do {
    new_seed = TrivialPrng(old_seed);
  } while (!gPrng.compare_exchange_weak(old_seed, new_seed, ktl::memory_order::relaxed));
  return TrivialRand(new_seed);
}
