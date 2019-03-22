// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

// Simple program that writes both to stdout and stderr.

int main() {
  printf("Writing into stdout.\n");
  fprintf(stderr, "Writing into stderr.\n");
}
