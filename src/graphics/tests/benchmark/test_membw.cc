// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

void* a;
void* b;
uint64_t buffer_size;
uint64_t iterations;

void copy_loop() {
  for (uint64_t iter = 0; iter < iterations; iter++) {
    memcpy(b, a, buffer_size);
  }
}

void set_loop() {
  for (uint64_t iter = 0; iter < iterations; iter++) {
    memset(b, 0, buffer_size);
  }
}

int main(int argc, char** argv) {
  bool do_copy = false;

  if (argc > 1) {
    if (strcmp(argv[1], "-copy") == 0) {
      do_copy = true;
    } else if (strcmp(argv[1], "-set") == 0) {
      do_copy = false;
    } else {
      printf("Unrecognized option: %s, use -copy|-set [buffer_size] [iterations]\n", argv[1]);
      return EXIT_FAILURE;
    }
  }

  buffer_size = 6000000;

  if (argc > 2) {
    buffer_size = strtoul(argv[2], nullptr, 0);
  }

  printf("Allocating buffers\n");fflush(stdout);
  a = malloc(buffer_size);
  b = malloc(buffer_size);

  printf("Initializing buffers\n");fflush(stdout);
  // Don't fill with zero, could be zero-page optimized
  memset(a, 1, buffer_size);
  memset(b, 1, buffer_size);

  iterations = 1;
  printf("Running 1 iteration\n");fflush(stdout);
  if (do_copy) {
    copy_loop();
  } else {
    set_loop();
  }

  iterations = 10000;
  if (argc > 3) {
    iterations = strtoul(argv[3], nullptr, 0);
  }
  printf("Running %lu iterations\n", iterations);fflush(stdout);

  auto start = std::chrono::high_resolution_clock::now();

  if (do_copy) {
    copy_loop();
  } else {
    set_loop();
  }

  std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;

  free(a);
  free(b);

  printf("%s: buffer_size %lu iterations %lu transfer rate %.2f MB/s\n", do_copy ? "COPY" : "SET",
         buffer_size, iterations,
         static_cast<double>(buffer_size * iterations) / 1024 / 1024 / elapsed.count());

  return EXIT_SUCCESS;
}
