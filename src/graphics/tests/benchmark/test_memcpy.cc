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

void loop() {
  for (uint64_t iter = 0; iter < iterations; iter++) {
    memcpy(b, a, buffer_size);
  }
}

int main(int argc, char** argv) {
  buffer_size = 6 * 1024 * 1024;
  iterations = 1000;

  a = malloc(buffer_size);
  b = malloc(buffer_size);

  memset(a, 0, buffer_size);
  memset(b, 0, buffer_size);

  auto start = std::chrono::high_resolution_clock::now();

  loop();

  free(a);
  free(b);

  std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
  printf("buffer_size %lu iterations %lu copy rate %.2f MB/s\n", buffer_size, iterations,
         static_cast<double>(buffer_size * iterations) / 1024 / 1024 / elapsed.count());

  return 0;
}
