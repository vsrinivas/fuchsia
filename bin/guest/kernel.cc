// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/kernel.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static inline bool is_within(uintptr_t x, uintptr_t addr, uintptr_t size) {
  return x >= addr && x < addr + size;
}

zx_status_t load_kernel(const std::string& kernel_path,
                        const machina::PhysMem& phys_mem) {
  fbl::unique_fd fd(open(kernel_path.c_str(), O_RDONLY));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open kernel image " << kernel_path;
    return ZX_ERR_IO;
  }
  struct stat stat;
  ssize_t ret = fstat(fd.get(), &stat);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to stat kernel image";
    return ZX_ERR_IO;
  }
  if (kKernelOffset + stat.st_size >= phys_mem.size()) {
    FXL_LOG(ERROR) << "Kernel location is outside of guest physical memory";
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (is_within(kRamdiskOffset, kKernelOffset, stat.st_size)) {
    FXL_LOG(ERROR) << "Kernel location overlaps RAM disk location";
    return ZX_ERR_OUT_OF_RANGE;
  }

  ret = read(fd.get(), phys_mem.ptr(kKernelOffset, stat.st_size), stat.st_size);
  if (ret != stat.st_size) {
    FXL_LOG(ERROR) << "Failed to read kernel image";
    return ZX_ERR_IO;
  }

  return ZX_OK;
}
