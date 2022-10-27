// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FVM_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FVM_H_

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/zx/result.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/device/block.h>

#include <string_view>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/format.h"

namespace fs_management {

// Format a block device to be an empty FVM.
zx_status_t FvmInit(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                    size_t slice_size);

// Format a block device to be an empty FVM of |disk_size| size.
zx_status_t FvmInitWithSize(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                            uint64_t disk_size, size_t slice_size);

// Format a block device to be an empty FVM. The FVM will initially be formatted as if the block
// device had |initial_volume_size| and leave gap for metadata extension up to |max_volume_size|.
// Note: volume sizes are assumed to be multiples of the underlying block device block size.
zx_status_t FvmInitPreallocated(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                                uint64_t initial_volume_size, uint64_t max_volume_size,
                                size_t slice_size);

// Queries driver to obtain slice_size, then overwrites and unbinds an FVM
zx_status_t FvmDestroy(const char* path);
zx_status_t FvmDestroyWithDevfs(int devfs_root_fd, const char* relative_path);

// Given the slice_size, overwrites and unbinds an FVM
zx_status_t FvmOverwrite(const char* path, size_t slice_size);
zx_status_t FvmOverwriteWithDevfs(int devfs_root_fd, const char* relative_path, size_t slice_size);

// Allocates a new vpartition in the fvm, and waits for it to become
// accessible (by watching for a corresponding block device).
//
// Returns an open fd to the new partition on success, -1 on error.
zx::result<fbl::unique_fd> FvmAllocatePartition(int fvm_fd, const alloc_req_t* request);
zx::result<fbl::unique_fd> FvmAllocatePartitionWithDevfs(int devfs_root_fd, int fvm_fd,
                                                         const alloc_req_t* request);

// Query the volume manager for info.
zx::result<fuchsia_hardware_block_volume::wire::VolumeManagerInfo> FvmQuery(int fvm_fd);

// A set of optional matchers for |open_partition| and friends.
// At least one must be specified.
struct PartitionMatcher {
  const uint8_t* type_guid = nullptr;
  const uint8_t* instance_guid = nullptr;
  const char* const* labels = nullptr;
  size_t num_labels = 0;
  DiskFormat detected_disk_format = kDiskFormatUnknown;
  // partition must be a child of this device.
  std::string_view parent_device;
  // The topological path must not start with this prefix.
  std::string_view ignore_prefix;
  // The topological path must not contain this substring.
  std::string_view ignore_if_path_contains;
};

// Waits for a partition with a GUID pair to appear, and opens it.
//
// If one of the GUIDs is null, it is ignored. For example:
//   wait_for_partition(NULL, systemGUID, ZX_SEC(5));
// Waits for any partition with the corresponding system GUID to appear.
// At least one of the GUIDs must be non-null.
//
// Returns an open fd to the partition on success, -1 on error.
zx::result<fbl::unique_fd> OpenPartition(const PartitionMatcher& matcher, zx_duration_t timeout,
                                         std::string* out_path);
zx::result<fbl::unique_fd> OpenPartitionWithDevfs(int devfs_root_fd,
                                                  const PartitionMatcher& matcher,
                                                  zx_duration_t timeout,
                                                  std::string* out_path_relative);

// Finds and destroys the partition with the given GUID pair, if it exists.
zx_status_t DestroyPartition(const uint8_t* uniqueGUID, const uint8_t* typeGUID);
zx_status_t DestroyPartitionWithDevfs(int devfs_root_fd, const uint8_t* uniqueGUID,
                                      const uint8_t* typeGUID);

// Marks one partition as active and optionally another as inactive in one atomic operation.
// If both partition GUID are the same, the partition will be activated and
// no partition will be marked inactive.
zx_status_t FvmActivate(int fvm_fd, fuchsia_hardware_block_partition::wire::Guid deactivate,
                        fuchsia_hardware_block_partition::wire::Guid activate);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FVM_H_
