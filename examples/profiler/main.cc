// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "garnet/lib/profiler/test.h"

int main(int argc, char** argv) {
  printf("Hello profiler: %d\n", test());
  return 0;
}
