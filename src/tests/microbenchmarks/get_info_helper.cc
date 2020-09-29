// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <pthread.h>
#include <zircon/assert.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <chrono>
#include <string>
#include <thread>

// This helper program receives the number of threads it should start, each thread periodically
// sleeps and loops forever.
int main(int argc, char** argv) {
  ZX_ASSERT(argc >= 2);

  uint32_t threads = std::atoi(argv[1]);
  zx::channel incoming(zx_take_startup_handle(PA_USER0));
  ZX_ASSERT(incoming);

  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, nullptr, threads);

  for (uint32_t i = 0; i < threads - 1; i++) {
    std::thread([&] {
      pthread_barrier_wait(&barrier);
      while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }).detach();
  }

  char message[] = "test";
  pthread_barrier_wait(&barrier);
  incoming.write(0, message, 4, nullptr, 0);
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
