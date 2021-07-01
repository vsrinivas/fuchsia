// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

int main() {
  printf("stdout msg1\n");
  fprintf(stderr, "stderr msg1\n");
  printf("stdout msg2\n");
  fprintf(stderr, "stderr msg2\n");
  return 0;
}
