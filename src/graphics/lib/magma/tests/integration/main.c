// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__linux__)
#error Linux only
#endif

#include "test_magma.h"

int main(int argc, char** argv) {
  if (!test_magma_from_c("/dev/magma0"))
    return -1;
  return 0;
}
