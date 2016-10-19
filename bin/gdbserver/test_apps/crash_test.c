// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

int main(void) {
  printf("crash_test: Sleeping for 5 seconds\n");

  sleep(5);

  printf("crash_test: Dereference and assign to NULL ptr\n");

  int* foo = NULL;
  *foo = 5;

  return 0;
}
