// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>
#include <lib/fdio/fdio.h>

#include "sdio.h"

int main(int argc, const char** argv) {
  if (argc == 2) {
    if (strcmp(argv[1], "--help") == 0) {
      sdio::PrintUsage();
      return 0;
    } else if (strcmp(argv[1], "--version") == 0) {
      sdio::PrintVersion();
      return 0;
    }
  }

  if (argc < 2) {
    fprintf(stderr, "Expected more arguments\n");
    sdio::PrintUsage();
    return 1;
  }

  fbl::unique_fd fd(open(argv[1], O_RDWR));
  if (fd.get() <= -1) {
    fprintf(stderr, "Failed to open SDIO device: %d\n", fd.get());
    return 1;
  }

  zx::channel handle;
  zx_status_t status = fdio_get_service_handle(fd.release(), handle.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get FDIO handle for SDIO device: %d\n", status);
    return 1;
  }

  return sdio::RunSdioTool(sdio::SdioClient(std::move(handle)), argc - 2, argv + 2);
}
