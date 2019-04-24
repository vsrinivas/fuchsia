// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper program to just exist for a specified duration and then exit.

#include <stdio.h>
#include <stdlib.h>

#include <lib/zx/time.h>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: run_awhile <seconds>\n");
    return EXIT_FAILURE;
  }
  int duration = atoi(argv[1]);
  zx::nanosleep(zx::deadline_after(zx::sec(duration)));
  return EXIT_SUCCESS;
}
