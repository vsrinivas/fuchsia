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

#include <new>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>

#include "path.h"

namespace {

zx_status_t FsckNativeFs(const char* device_path, const FsckOptions& options, LaunchCallback cb,
                         const char* cmd_path) {
  zx::channel crypt_client(options.crypt_client);
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
  argv.push_back(cmd_path);
  if (options.verbose) {
    argv.push_back("-v");
  }
  // TODO(smklein): Add support for modify, force flags. Without them,
  // we have "never_modify=true" and "force=true" effectively on by default.
  argv.push_back("fsck");
  argv.push_back(nullptr);

  zx_handle_t handles[] = {block_device.release(), crypt_client.release()};
  uint32_t ids[] = {FS_HANDLE_BLOCK_DEVICE_ID, PA_HND(PA_USER0, 2)};
  auto argc = static_cast<int>(argv.size() - 1);
  status = static_cast<zx_status_t>(
      cb(argc, argv.data(), handles, ids, handles[1] == ZX_HANDLE_INVALID ? 1 : 2));
  return status;
}

zx_status_t FsckFat(const char* device_path, const FsckOptions& options, LaunchCallback cb) {
  fbl::Vector<const char*> argv;
  const std::string tool_path = fs_management::GetBinaryPath("fsck-msdosfs");
  argv.push_back(tool_path.c_str());
  if (options.never_modify) {
    argv.push_back("-n");
  } else if (options.always_modify) {
    argv.push_back("-y");
  }
  if (options.force) {
    argv.push_back("-f");
  }
  argv.push_back(device_path);
  argv.push_back(nullptr);
  auto argc = static_cast<int>(argv.size() - 1);
  zx_status_t status = static_cast<zx_status_t>(cb(argc, argv.data(), nullptr, nullptr, 0));
  return status;
}

}  // namespace

__EXPORT
zx_status_t fsck(const char* device_path, disk_format_t df, const FsckOptions& options,
                 LaunchCallback cb) {
  // N.B. Make sure to release crypt_client in any new error paths here.
  switch (df) {
    case DISK_FORMAT_FACTORYFS:
      return FsckNativeFs(device_path, options, cb,
                          fs_management::GetBinaryPath("factoryfs").c_str());
    case DISK_FORMAT_MINFS:
      return FsckNativeFs(device_path, options, cb, fs_management::GetBinaryPath("minfs").c_str());
    case DISK_FORMAT_FXFS:
      return FsckNativeFs(device_path, options, cb, fs_management::GetBinaryPath("fxfs").c_str());
    case DISK_FORMAT_FAT:
      return FsckFat(device_path, options, cb);
    case DISK_FORMAT_BLOBFS:
      return FsckNativeFs(device_path, options, cb, fs_management::GetBinaryPath("blobfs").c_str());
    default:
      auto* format = fs_management::CustomDiskFormat::Get(df);
      if (format == nullptr) {
        if (options.crypt_client != ZX_HANDLE_INVALID)
          zx_handle_close(options.crypt_client);
        return ZX_ERR_NOT_SUPPORTED;
      }
      return FsckNativeFs(device_path, options, cb, format->binary_path().c_str());
  }
}
