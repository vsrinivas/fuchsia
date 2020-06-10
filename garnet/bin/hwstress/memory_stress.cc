// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "status.h"
#include "temperature_sensor.h"

namespace hwstress {

constexpr size_t kBytesToTest = 1024 * 1024;

void StressMemory(zx::duration duration, TemperatureSensor* /*temperature_sensor*/) {
  zx::time end_time = zx::deadline_after(duration);

  // A trivial placeholder memory test.
  while (zx::clock::get_monotonic() < end_time) {
    // Allocate memory.
    auto memory = std::make_unique<unsigned char[]>(kBytesToTest);

    // Set all the bits to 1.
    memset(memory.get(), 0xff, kBytesToTest);

    // Verify the bits are actually 1.
    for (size_t i = 0; i < kBytesToTest; i++) {
      if (memory[i] != 0xff) {
        ZX_PANIC("Failed memory test.");
      }
    }
  }
}

}  // namespace hwstress
