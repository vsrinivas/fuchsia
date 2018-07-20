// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define KERNEL_CRASHLOG_BUF_SIZE 4096

int main(int argc, char** argv) {
  const char filepath[] = "/boot/log/last-panic.txt";
  int fd;
  if ((fd = open(filepath, O_RDONLY)) < 0) {
    printf("kernel_crash_checker: no crash log found\n");
    return 0;
  }

  printf("kernel_crash_checker: dumping log from previous kernel panic:\n");
  char buf[KERNEL_CRASHLOG_BUF_SIZE];
  ssize_t r;
  while ((r = read(fd, buf, KERNEL_CRASHLOG_BUF_SIZE)) > 0) {
    write(STDOUT_FILENO, buf, r);
  }
  if (r < 0) {
    printf("kernel_crash_checker: failed to read '%s'\n", filepath);
    return 1;
  }

  return 0;
}
