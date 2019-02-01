// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <iostream>

int main() {
  while (true) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(3)));
    std::cout << "Hello, World!\n";
  }
  return 0;
}