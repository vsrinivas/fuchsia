// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>
#include <stdio.h>

// This binary will test glob path passed to it as arg and write the path to
// stdout if found, else will write error to stderr and return 1.
int main(int argc, const char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <glob_path>", argv[0]);
    return 1;
  }
  auto glob_str = argv[1];
  glob_t globbuf;
  auto status = glob(glob_str, 0, nullptr, &globbuf);
  if (status != 0) {
    fprintf(stderr, "glob failed: %d", status);
    return 1;
  }
  if (globbuf.gl_pathc == 0) {
    fprintf(stderr, "no match found");
    globfree(&globbuf);
    return 1;
  }
  for (size_t i = 0; i < globbuf.gl_pathc; i++) {
    fprintf(stdout, "%s\n", globbuf.gl_pathv[0]);
  }
  globfree(&globbuf);
  return 0;
}
