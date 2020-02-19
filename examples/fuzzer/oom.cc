// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* A simple fuzzer that should quickly OOM. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const size_t kLeakSize = 10UL << 20;  // 10 MiB

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Simulate a fuzzer that only leaks on a specific input
  if (size < 1 || data[0] != 42) {
    return 0;
  }

  // Note: In addition to allocating, we must also write to the memory to ensure
  // it is committed
  memset(malloc(kLeakSize), 42, kLeakSize);
  printf("Leaked %zu bytes\n", kLeakSize);

  // RssThread in libFuzzer only checks RSS once per second, so let's not go
  // so fast that we risk OOMing the system before that check happens
  usleep(100e3);  // 0.1 seconds

  return 0;
}
