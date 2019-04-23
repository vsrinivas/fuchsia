// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

#include <stdio.h>

#include <string>
#include <thread>

namespace {

void NOINLINE ThreadFunction() {
  MultithreadedFunctionToBreakOn();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2)
    return -1;

  int thread_count = std::stoi(argv[1]);
  if (thread_count == 0)
    return -1;

  // Start all the threads.
  std::thread threads[thread_count];
  for (int i = 0; i < thread_count; i++) {
    threads[i] = std::thread(ThreadFunction);
  }

  printf("Function address: %p\n", MultithreadedFunctionToBreakOn);
  fflush(stdout);

  // We join all the threads.
  for (int i = 0; i < thread_count; i++) {
    threads[i].join();
  }
}
