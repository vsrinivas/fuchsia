// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // for close

const char* kGpuDeviceName = "/dev/class/gpu/000";

int main(int argc, char** argv) {
  int fd = open(kGpuDeviceName, O_RDONLY);
  if (fd < 0) {
    printf("Failed to open magma device %s\n", kGpuDeviceName);
    return -1;
  }

  uint32_t dump_type = 0;
  if (argc >= 2) {
    dump_type = atoi(argv[1]);
  }

  fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
  if (!fdio) {
    printf("invalid fd: %d", fd);
    return -1;
  }

  zx_status_t status =
      fuchsia_gpu_magma_DeviceDumpState(fdio_unsafe_borrow_channel(fdio), dump_type);
  fdio_unsafe_release(fdio);

  if (status != ZX_OK) {
    printf("magma_DeviceDumpStatus failed: %d", status);
    return -1;
  }
  printf("Dumping system driver status to system log\n");

  close(fd);
  return 0;
}
