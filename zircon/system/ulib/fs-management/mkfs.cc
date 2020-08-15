// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <iterator>
#include <new>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>

#include "path.h"

namespace {

zx_status_t MkfsNativeFs(const char* binary, const char* device_path, LaunchCallback cb,
                         const mkfs_options_t* options, bool support_fvm) {
  fbl::unique_fd device_fd;
  device_fd.reset(open(device_path, O_RDWR));
  if (!device_fd) {
    fprintf(stderr, "Failed to open device\n");
    return ZX_ERR_BAD_STATE;
  }
  zx::channel block_device;
  zx_status_t status =
      fdio_get_service_handle(device_fd.release(), block_device.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options->verbose) {
    argv.push_back("-v");
  }

  fbl::StringBuffer<20> fvm_data_slices;
  // TODO(manalib) restructure this code to do something more sensible instead of
  // support_fvm bool.
  if (support_fvm) {
    if (options->fvm_data_slices > default_mkfs_options.fvm_data_slices) {
      argv.push_back("--fvm_data_slices");
      fvm_data_slices.AppendPrintf("%u", options->fvm_data_slices);
      argv.push_back(fvm_data_slices.c_str());
    }
  }
  argv.push_back("mkfs");
  argv.push_back(nullptr);

  zx_handle_t hnd = block_device.release();
  uint32_t id = FS_HANDLE_BLOCK_DEVICE_ID;
  status =
      static_cast<zx_status_t>(cb(static_cast<int>(argv.size() - 1), argv.data(), &hnd, &id, 1));
  return status;
}

zx_status_t MkfsFat(const char* device_path, LaunchCallback cb, const mkfs_options_t* options) {
  const std::string tool_path = fs_management::GetBinaryPath("mkfs-msdosfs");
  std::string sectors_per_cluster;
  std::vector<const char*> argv = {tool_path.c_str()};
  if (options->sectors_per_cluster != 0) {
    argv.push_back("-c");
    sectors_per_cluster = std::to_string(options->sectors_per_cluster);
    argv.push_back(sectors_per_cluster.c_str());
  }
  argv.push_back(device_path);
  argv.push_back(nullptr);
  return cb(argv.size() - 1, argv.data(), NULL, NULL, 0);
}

}  // namespace

__EXPORT
zx_status_t mkfs(const char* device_path, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options) {
  switch (df) {
    case DISK_FORMAT_FACTORYFS:
      return MkfsNativeFs(fs_management::GetBinaryPath("factoryfs").c_str(), device_path, cb,
                          options, false);
    case DISK_FORMAT_MINFS:
      return MkfsNativeFs(fs_management::GetBinaryPath("minfs").c_str(), device_path, cb, options,
                          true);
    case DISK_FORMAT_FAT:
      return MkfsFat(device_path, cb, options);
    case DISK_FORMAT_BLOBFS:
      return MkfsNativeFs(fs_management::GetBinaryPath("blobfs").c_str(), device_path, cb, options,
                          true);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
