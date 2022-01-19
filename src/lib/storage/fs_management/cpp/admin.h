// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/launch.h"

namespace fs_management {

inline constexpr std::string_view kPathData = "/data";
inline constexpr std::string_view kPathInstall = "/install";
inline constexpr std::string_view kPathDurable = "/durable";
inline constexpr std::string_view kPathSystem = "/system";
inline constexpr std::string_view kPathBlob = "/blob";
inline constexpr std::string_view kPathFactory = "/factory";
inline constexpr std::string_view kPathVolume = "/volume";
inline constexpr std::string_view kPathDevBlock = "/dev/class/block";

struct InitOptions {
  bool readonly = false;
  bool verbose_mount = false;
  bool collect_metrics = false;

  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready = true;

  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  const char* write_compression_algorithm = nullptr;

  // An optional compression level for the filesystem to use when storing files (if the filesystem
  // and the configured |write_compression_algorithm| supports it).
  // Setting to < 0 indicates no value (the filesystem chooses a default if necessary).
  int write_compression_level = -1;

  // An optional eviction policy specifier for the filesystem to use for in-memory structures (if
  // the filesystem supports it).
  const char* cache_eviction_policy = nullptr;

  // If true, run fsck after every transaction (if supported). This is for testing/debugging
  // purposes.
  bool fsck_after_every_transaction = false;

  // If true, decompression is run in a sandbox component.
  bool sandbox_decompression = false;

  // Provide a launch callback function pointer for configuring how the underlying filesystem
  // process is launched.
  LaunchCallback callback = &launch_stdio_async;
};

struct MkfsOptions {
  uint32_t fvm_data_slices = 1;
  bool verbose = false;

  // The number of sectors per cluster on a FAT file systems or zero for the default.
  int sectors_per_cluster = 0;

  // Set to use the deprecated padded blobfs format.
  bool deprecated_padded_blobfs_format = false;

  // The initial number of inodes to allocate space for. If 0, a default is used. Only supported
  // for blobfs.
  uint64_t num_inodes = 0;

  // Handle to the crypt client for filesystems that need it.  The handle is *always* consumed, even
  // on error.
  zx_handle_t crypt_client = ZX_HANDLE_INVALID;
};

struct FsckOptions {
  bool verbose = false;

  // At MOST one of the following '*_modify' flags may be true.
  bool never_modify = false;   // Fsck still looks for problems, but does not try to resolve them.
  bool always_modify = false;  // Fsck never asks to resolve problems; it will always do it.
  bool force = false;          // Force fsck to check the filesystem integrity, even if "clean".

  // Handle to the crypt client for filesystems that need it.  The handle is *always* consumed, even
  // on error.
  zx_handle_t crypt_client = ZX_HANDLE_INVALID;
};

// Format the provided device with a requested disk format.
zx_status_t Mkfs(const char* device_path, DiskFormat df, LaunchCallback cb,
                 const MkfsOptions& options);

// Check and repair a device with a requested disk format.
zx_status_t Fsck(std::string_view device_path, DiskFormat df, const FsckOptions& options,
                 LaunchCallback cb);

// Initialize the filesystem present on |device_handle|, returning a connection to the outgoing
// directory in |out_export_root|. The outgoing directory implements |fuchsia.io/Directory| and
// contains handles to services exported by the filesystem.
//
// The outgoing directory has the following layout -
//     |/root| - the data root of the filesystem
//
// Specific filesystems may have additional entries in the outgoing directory for
// filesystem-specific operations.
//
// |device_handle| is always consumed.
zx::status<fidl::ClientEnd<fuchsia_io::Directory>> FsInit(zx::channel device_handle, DiskFormat df,
                                                          const InitOptions& options,
                                                          zx::channel crypt_client = {});

// Get a connection to the root of the filesystem, given a filesystem outgoing directory.
zx::status<fidl::ClientEnd<fuchsia_io::Directory>> FsRootHandle(
    fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
    uint32_t flags = fuchsia_io::wire::kOpenRightReadable |
                     fuchsia_io::wire::kOpenFlagPosixWritable |
                     fuchsia_io::wire::kOpenFlagPosixExecutable);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_
