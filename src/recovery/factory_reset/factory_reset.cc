// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/system.h>

#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fs-management/mount.h>

namespace factory_reset {

const char* kBlockPath = "class/block";
const int kNumZxcryptSuperblocks = 3;

zx_status_t WriteRandomBlock(int fd, ssize_t block_size) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[block_size]);
  zx_cprng_draw(buf.get(), block_size);
  ssize_t res;
  if ((res = write(fd, buf.get(), block_size)) < block_size) {
    fprintf(stderr, "write(%d, %p, %zu) failed: %s\n ", fd, buf.get(), block_size, strerror(errno));
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

// Determines the block size of the passed in fd.
zx_status_t FindBlockSize(int fd, ssize_t* block_size) {
  zx_status_t rc, call_status;
  fdio_cpp::UnownedFdioCaller caller(fd);
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_hardware_block_BlockInfo block_info;
  if ((rc = fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &call_status,
                                                &block_info)) != ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }
  *block_size = block_info.block_size;
  return ZX_OK;
}

zx_status_t ShredBlockDevice(fbl::unique_fd fd, int num_blocks) {
  zx_status_t status;
  if (lseek(fd.get(), 0, SEEK_SET) != 0) {
    return ZX_ERR_IO;
  }

  ssize_t block_size = 0;
  status = FindBlockSize(fd.get(), &block_size);
  if (status != ZX_OK) {
    return status;
  }

  for (int i = 0; i < num_blocks; ++i) {
    if ((status = WriteRandomBlock(fd.get(), block_size)) != ZX_OK) {
      fprintf(stderr, "Couldn't write to %d block: %d (%s)\n", i, status,
              zx_status_get_string(status));
      return status;
    }
  }
  return ZX_OK;
}

FactoryReset::FactoryReset(fbl::unique_fd dev_fd,
                           fuchsia::device::manager::AdministratorPtr admin) {
  dev_fd_ = std::move(dev_fd);
  admin_ = std::move(admin);
}

zx_status_t FactoryReset::Shred() const {
  fbl::unique_fd block_dir(openat(dev_fd_.get(), kBlockPath, O_RDONLY | O_DIRECTORY));
  if (!block_dir) {
    fprintf(stderr, "Error opening %s\n", kBlockPath);
    return ZX_ERR_NOT_FOUND;
  }
  struct dirent* de;
  DIR* dir = fdopendir(block_dir.get());
  // Attempts to shred every zxcrypt volume found.
  while ((de = readdir(dir)) != nullptr) {
    fbl::unique_fd block_fd(openat(dirfd(dir), de->d_name, O_RDWR));
    if (!block_fd || detect_disk_format(block_fd.get()) != DISK_FORMAT_ZXCRYPT) {
      continue;
    }
    zx_status_t status = ShredBlockDevice(std::move(block_fd), kNumZxcryptSuperblocks);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void FactoryReset::Reset(ResetCallback callback) {
  zx_status_t status = Shred();
  if (status != ZX_OK) {
    fprintf(stderr, "FactoryReset: Shred failed: %d (%s)\n", status, zx_status_get_string(status));
    callback(std::move(status));
    return;
  }

  // Reboot to initiate the recovery.
  admin_->Suspend(fuchsia::device::manager::SUSPEND_FLAG_REBOOT, std::move(callback));
}

}  // namespace factory_reset
