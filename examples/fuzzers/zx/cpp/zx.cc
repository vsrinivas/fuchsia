// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <iostream>

static void Random(bool deterministic, char* bytes, size_t count) {
  if (deterministic) {
    for (size_t i = 0; i < count; i += sizeof(int)) {
      const int r = rand();
      memcpy(&bytes[i], &r, sizeof(r));
    }
  } else {
    zx_cprng_draw(bytes, count);
  }
}

static uintptr_t MaskNoiseToUserPointer(uintptr_t n) {
  n &= 0x00007FFFFFFFFFFFULL;
  return n;
}

int main(int argc, const char** argv) {
  unsigned int seed = 0;
  if (argc > 1) {
    seed = atoi(argv[1]);
  }
  const bool deterministic = seed != 0;
  if (deterministic) {
    srand(seed);
  }

  std::cout << "Hello, Fuzzy World! Fuzzing with seed " << seed
            << (deterministic ? "" : " (non-deterministic)") << "\n";
  std::cout << "To reproduce issues, set the seed explicitly in "
               "//examples/fuzzers/examples/fuzzers/zx/cpp/meta/hello_fuzzy_world.cml"
            << "\n";
  uint64_t count = 0;
  while (true) {
    uint64_t noise[5];
    Random(deterministic, reinterpret_cast<char*>(noise), sizeof(noise));

    // TODO(corkami): Extend this to randomly select any system call.
    noise[0] = MaskNoiseToUserPointer(static_cast<uintptr_t>(noise[0]));
    printf("%08lu zx_futex_wait(0x%016lx, 0x%016lx, 0x%016lx, 0x%016lx): ", count, noise[0],
           noise[1], noise[2], noise[3]);
    fflush(stdout);
    const zx_status_t s =
        zx_futex_wait(reinterpret_cast<zx_futex_t*>(noise[0]), static_cast<zx_futex_t>(noise[1]),
                      static_cast<zx_handle_t>(noise[2]), noise[3]);
    printf("0x%d %s\n", s, zx_status_get_string(s));
    fflush(stdout);
    count++;
  }
}
