// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

// Helper for hub_integration_test.cc.
// Takes a glob expression as an arg.
// Returns 0 if the glob matched one or more of its namespace entries.
int main(int argc, const char** argv) {
  if (argc != 2) {
    return -1;
  }
  auto glob_str = argv[1];
  glob_t globbuf;
  auto status = glob(glob_str, 0, nullptr, &globbuf);
  if (status != 0) {
    return 1;
  }
  size_t matches_count = globbuf.gl_pathc;
  globfree(&globbuf);
  if (matches_count == 0) {
    return 1;
  }
  return 0;
}
