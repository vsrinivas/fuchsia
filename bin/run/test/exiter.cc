// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: exiter <return_code>\n");
    return 1;
  }

  errno = 0;
  int64_t argument = strtoll(argv[1], NULL, 0);
  if (errno != 0) {
    fprintf(stderr, "Invalid input %s\n", argv[1]);
    return 1;
  }

  zx_process_exit(argument);
  return 0;
}
