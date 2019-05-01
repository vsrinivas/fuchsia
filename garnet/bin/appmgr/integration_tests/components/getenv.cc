// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>

int main(int argc, const char** argv) {
  for (int i = 1; i < argc; ++i) {
    char* val = getenv(argv[i]);
    printf("%s%s=%s", (i == 1 ? "" : " "), argv[i],
           (val != nullptr ? val : "NULL"));
  }
  printf("\n");
  return 0;
}
