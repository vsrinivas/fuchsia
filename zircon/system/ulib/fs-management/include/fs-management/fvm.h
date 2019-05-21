// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/device/block.h>

__BEGIN_CDECLS

// TODO(smklein): Rename functions to match library (prefix).

// Format a block device to be an empty FVM.
zx_status_t fvm_init(int fd, size_t slice_size);

// Format a block device to be an empty FVM of |disk_size| size.
zx_status_t fvm_init_with_size(int fd, uint64_t disk_size, size_t slice_size);

// Format a block device to be an empty FVM. The FVM will initially be formatted as if the block
// device had |initial_volume_size| and leave gap for metadata extension up to |max_volume_size|.
// Note: volume sizes are assumed to be multiples of the underlying block device block size.
zx_status_t fvm_init_preallocated(int fd, uint64_t initial_volume_size,
                                  uint64_t max_volume_size, size_t slice_size);

// Queries driver to obtain slice_size, then overwrites and unbinds an FVM
zx_status_t fvm_destroy(const char* path);
zx_status_t fvm_destroy_with_devfs(int devfs_root_fd, const char* relative_path);

// Given the slice_size, overwrites and unbinds an FVM
zx_status_t fvm_overwrite(const char* path, size_t slice_size);
zx_status_t fvm_overwrite_with_devfs(int devfs_root_fd, const char* relative_path,
                                     size_t slice_size);

// Allocates a new vpartition in the fvm, and waits for it to become
// accessible (by watching for a corresponding block device).
//
// Returns an open fd to the new partition on success, -1 on error.
int fvm_allocate_partition(int fvm_fd, const alloc_req_t* request);
int fvm_allocate_partition_with_devfs(int devfs_root_fd, int fvm_fd, const alloc_req_t* request);

// Query the volume manager for info.
zx_status_t fvm_query(int fvm_fd, fuchsia_hardware_block_volume_VolumeInfo* out);

// Waits for a partition with a GUID pair to appear, and opens it.
//
// If one of the GUIDs is null, it is ignored. For example:
//   wait_for_partition(NULL, systemGUID, ZX_SEC(5));
// Waits for any partition with the corresponding system GUID to appear.
// At least one of the GUIDs must be non-null.
//
// Returns an open fd to the partition on success, -1 on error.
int open_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID, zx_duration_t timeout,
                   char* out_path);
int open_partition_with_devfs(int devfs_root_fd, const uint8_t* uniqueGUID,
                              const uint8_t* typeGUID, zx_duration_t timeout,
                              char* out_path_relative);

// Finds and destroys the partition with the given GUID pair, if it exists.
zx_status_t destroy_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID);
zx_status_t destroy_partition_with_devfs(int devfs_root_fd, const uint8_t* uniqueGUID,
                                         const uint8_t* typeGUID);

__END_CDECLS
