// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 4) {
    printf("ERROR: unexpected argc=%d\nUSAGE: %s CHDIR CHROOT ACCESS", argc, argv[0]);
    return 1;
  }

  if (const int rv = chdir(argv[1]); rv != 0) {
    printf("chdir returned %d, errno=%d(%s)\n", rv, errno, strerror(errno));
    return 1;
  }
  printf("chdir(%s) SUCCESS\n", argv[1]);

  if (const int rv = chroot(argv[2]); rv != 0) {
    printf("chroot returned %d, errno=%d(%s)\n", rv, errno, strerror(errno));
    return 1;
  }
  printf("chroot(%s) SUCCESS\n", argv[2]);

  if (const int rv = access(argv[3], F_OK); rv != 0) {
    printf("access returned %d, errno=%d(%s)\n", rv, errno, strerror(errno));
    return 1;
  }
  printf("access(%s) SUCCESS\n", argv[3]);

  char buf[PATH_MAX];

  const char* cwd = getcwd(buf, sizeof(buf));
  if (cwd == nullptr) {
    printf("cwd returned nullptr, errno=%d(%s)\n", errno, strerror(errno));
    return 1;
  }
  printf("cwd=%s\n", cwd);

  const char* rp = realpath(".", buf);
  if (rp == nullptr) {
    printf("realpath returned nullptr, errno=%d(%s)\n", errno, strerror(errno));
    return 1;
  }
  printf("realpath(.)=%s\n", rp);

  return 0;
}
