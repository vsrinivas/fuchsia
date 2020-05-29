// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/zx/debuglog.h>
#include <stdio.h>
#include <zircon/types.h>

int main(int argc, char** argv) {
  zx::debuglog debuglog;
  if (auto status = zx::debuglog::create(zx::resource(), 0, &debuglog); status != ZX_OK) {
    fprintf(stderr, "Failed to create debuglog\n");
    return -1;
  }

  int fd;
  if (auto status = fdio_fd_create(debuglog.release(), &fd); status != ZX_OK) {
    fprintf(stderr, "Failed to create file\n");
    return -1;
  }

  FILE* out = fdopen(fd, "w+");

  char* line = nullptr;
  size_t size;

  while (getline(&line, &size, stdin) != -1) {
    fputs(line, out);
  }
  fclose(out);

  return 0;
}
