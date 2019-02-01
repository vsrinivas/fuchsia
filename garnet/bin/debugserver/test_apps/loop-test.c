// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

int main(void) {
  printf("loop_test: Entering infinite loop\n");

  for (;;)
    ;

  return 0;
}
