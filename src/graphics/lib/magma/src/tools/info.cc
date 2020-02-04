// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // for close

#include <filesystem>

#include "src/lib/fxl/command_line.h"

const char* kGpuClassPath = "/dev/class/gpu";

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  static const char kGpuDeviceFlag[] = "gpu-device";
  static const char kDumpTypeFlag[] = "dump-type";

  std::string gpu_device_value;
  if (!command_line.GetOptionValue(kGpuDeviceFlag, &gpu_device_value)) {
    for (auto& p : std::filesystem::directory_iterator(kGpuClassPath)) {
      gpu_device_value = p.path();
    }
    if (gpu_device_value.empty()) {
      fprintf(stderr, "No magma device found\n");
      return -1;
    }
  }
  printf("Opening magma device: %s\n", gpu_device_value.c_str());
  int fd = open(gpu_device_value.c_str(), O_RDONLY);
  if (fd < 0) {
    printf("Failed to open magma device %s\n", gpu_device_value.c_str());
    return -1;
  }

  uint32_t dump_type = 0;
  std::string dump_type_string;
  if (command_line.GetOptionValue(kDumpTypeFlag, &dump_type_string)) {
    dump_type = atoi(dump_type_string.c_str());
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
