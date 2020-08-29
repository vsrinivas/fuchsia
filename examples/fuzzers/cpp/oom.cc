// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// A simple fuzzer that should quickly OOM.

// The code under test. Normally this would be in a separate library.
namespace {

const size_t kLeakSize = 10UL << 20;  // 10 MiB

void leaker(uint8_t num) {
  // Simulate a fuzzer that only leaks on a specific input
  if (num != 42) {
    return;
  }

  // Note: In addition to allocating, we must also write to the memory to ensure
  // it is committed
  memset(malloc(kLeakSize), 42, kLeakSize);
  printf("Leaked %zu bytes\n", kLeakSize);

  // RssThread in libFuzzer only checks RSS once per second, so let's not go
  // so fast that we risk OOMing the system before that check happens
  usleep(100e3);  // 0.1 seconds
}

}  // namespace

// The fuzz target function
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size != 0) {
    leaker(data[0]);
  }
  return 0;
}
