// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* A simple fuzzer that detects a heap buffer overflow. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


int foo_function(const uint8_t *data, size_t size) {
  int fizzle = 1 + size;
  if (fizzle < 3) {
  	return 0;
  }
  char* blah = (char*)malloc(1);
  blah[8] = 123;
  return 0;
}

int bar_function(const uint8_t *data, size_t size) {
  int fizzle = 1 + size;
  if (fizzle < 3) {
  	return 0;
  }
  return foo_function(data, size);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return bar_function(data, size);
}

