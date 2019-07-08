// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

int blind_write(volatile unsigned int* addr) {
  *addr = 0xBAD1DEA;
  return 0;
}

// Simple program that writes to address 0x0 and is expected to crash when doing so.
int main(int argc, const char** argv) {
  blind_write(nullptr);

  printf("crasher is exiting normally, but that shouldn't have happened.");
  return EXIT_SUCCESS;
}
