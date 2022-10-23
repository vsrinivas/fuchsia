// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <vector>

namespace fs_management {

struct MountOptions {
  bool readonly = false;
  bool verbose_mount = false;

  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready = true;

  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  std::optional<std::string> write_compression_algorithm;

  // An optional compression level for the filesystem to use when storing files (if the filesystem
  // and the configured |write_compression_algorithm| supports it).
  // Setting to < 0 indicates no value (the filesystem chooses a default if necessary).
  int write_compression_level = -1;

  // An optional cache eviction policy specifier for the filesystem to use for in-memory data (if
  // the filesystem supports it).
  std::optional<std::string> cache_eviction_policy;

  // If set, run fsck after every transaction.
  bool fsck_after_every_transaction = false;

  // If true, puts decompression in a sandboxed process.
  bool sandbox_decompression = false;

  // If set, a callable that returns a handle to the crypt client.
  std::function<zx::channel()> crypt_client;

  // If set, and the filesystem type supports it, use the provided child name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. Optionally, also define a component_collection_name if
  // the child component is in a collection.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_child_name;

  // If set, and the filesystem type supports it, use the provided collection name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. This won't do anything if component_child_name isn't set.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_collection_name;

  // If set, use the specified component URL rather than a default.
  std::string component_url;

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;

  // Generate a StartOptions fidl struct to pass the a fuchsia.fs.startup.Startup interface based
  // on this set of options.
  __EXPORT
  zx::result<fuchsia_fs_startup::wire::StartOptions> as_start_options() const;
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

  // If set, a callable that returns a handle to the crypt client.
  // NB: This is only used when Mkfs'ing Fxfs in the legacy (non-componentized) way and we should
  // probably remove this.
  std::function<zx::channel()> crypt_client;

  // If set, and the filesystem type supports it, use the provided child name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. Optionally, also define a component_collection_name if
  // the child component is in a collection.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_child_name;

  // If set, and the filesystem type supports it, use the provided collection name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. This won't do anything if component_child_name isn't set.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_collection_name;

  // If set, use the specified component URL rather than a default.
  std::string component_url;

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;

  // Generate a FormatOptions fidl struct to pass the a fuchsia.fs.startup.Startup interface based
  // on this set of options.
  __EXPORT
  fuchsia_fs_startup::wire::FormatOptions as_format_options() const;
};

struct FsckOptions {
  bool verbose = false;

  // At MOST one of the following '*_modify' flags may be true.
  bool never_modify = false;   // Fsck still looks for problems, but does not try to resolve them.
  bool always_modify = false;  // Fsck never asks to resolve problems; it will always do it.
  bool force = false;          // Force fsck to check the filesystem integrity, even if "clean".

  // If set, and the filesystem type supports it, use the provided child name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. Optionally, also define a component_collection_name if
  // the child component is in a collection.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_child_name;

  // If set, and the filesystem type supports it, use the provided collection name to connect to an
  // existing filesystem component instance that implements and is serving the
  // fuchsia.fs.startup.Startup protocol. This won't do anything if component_child_name isn't set.
  //
  // See //src/storage/docs/launching.md for more information.
  std::optional<std::string> component_collection_name;

  // If set, use the specified component URL rather than a default.
  std::string component_url;

  // Generate the argv list for launching a process based on this set of options.
  __EXPORT
  std::vector<std::string> as_argv(const char* binary) const;

  // Generate the argv list for launching a process based on this set of options for a FAT32
  // partition.
  //
  // TODO(fxbug.dev/96033): normalize fat32 launching so that it matches the rest of the platform
  // filesystems.
  __EXPORT
  std::vector<std::string> as_argv_fat32(const char* binary, const char* device_path) const;

  // Generate a CheckOptions fidl struct to pass the a fuchsia.fs.startup.Startup interface based
  // on this set of options.
  //
  // The current set of filesystems that support launching with fuchsia.fs.startup.Startup don't
  // support any check options so this doesn't currently do anything. This function is provided for
  // consistency.
  __EXPORT
  fuchsia_fs_startup::wire::CheckOptions as_check_options() const;
};

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_OPTIONS_H_
