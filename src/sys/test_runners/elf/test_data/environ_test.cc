// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (strcmp(getenv("HELLO"), "WORLD") != 0) {
    printf("Environment variable `HELLO` not set to expected value. Received: %s\n",
           getenv("HELLO"));
    return 1;
  }
  if (strcmp(getenv("FOO"), "BAR") != 0) {
    printf("Environment variable `FOO` not set to expected value. Received: %s\n", getenv("FOO"));
    return 1;
  }
  return 0;
}
