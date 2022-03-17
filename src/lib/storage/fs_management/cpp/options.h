// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_

#include <zircon/types.h>

#include <vector>

namespace fs_management {

struct MountOptions {
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

  // An optional cache eviction policy specifier for the filesystem to use for in-memory data (if
  // the filesystem supports it).
  const char* cache_eviction_policy = nullptr;

  // If set, run fsck after every transaction.
  bool fsck_after_every_transaction = false;

  // If true, puts decompression in a sandboxed process.
  bool sandbox_decompression = false;

  // If set, handle to the crypt client. The handle is *always* consumed, even on error.
  zx_handle_t crypt_client = ZX_HANDLE_INVALID;

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;
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

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;
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

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;

  // Generate the argv list for launching a process based on this set of options for a FAT32
  // partition.
  __EXPORT
  std::vector<std::string> as_argv_fat32(const char* binary, const char* device_path) const;
};

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_
