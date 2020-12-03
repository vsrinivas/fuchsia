// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 4) {
    printf("ERROR: unexpected argc=%d\nUSAGE: %s CHDIR CHROOT ACCESS", argc, argv[0]);
    return 1;
  }

  int rv = chdir(argv[1]);
  if (rv != 0) {
    printf("chdir returned %d, errno=%d\n", rv, errno);
    return 1;
  } else {
    printf("chdir(%s) SUCCESS\n", argv[1]);
  }

  rv = chroot(argv[2]);
  if (rv != 0) {
    printf("chroot returned %d, errno=%d\n", rv, errno);
    return 1;
  } else {
    printf("chroot(%s) SUCCESS\n", argv[2]);
  }

  rv = access(argv[3], F_OK);
  if (rv != 0) {
    printf("access returned %d, errno=%d\n", rv, errno);
    return 1;
  } else {
    printf("access(%s) SUCCESS\n", argv[3]);
  }

  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  printf("cwd=%s\n", cwd);

  char realpath_dot[PATH_MAX];
  realpath(".", realpath_dot);
  printf("realpath=%s\n", realpath_dot);
  return 0;
}
