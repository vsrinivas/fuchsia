// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <string.h>

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/device/block.h>

__BEGIN_CDECLS

// TODO(smklein): Rename functions to match library (prefix).

// Format a block device to be an empty FVM.
zx_status_t fvm_init(int fd, size_t slice_size);
// Queries driver to obtain slice_size, then overwrites and unbinds an FVM
zx_status_t fvm_destroy(const char* path);
// Given the slice_size, overwrites and unbinds an FVM
zx_status_t fvm_overwrite(const char* path, size_t slice_size);

// Allocates a new vpartition in the fvm, and waits for it to become
// accessible (by watching for a corresponding block device).
//
// Returns an open fd to the new partition on success, -1 on error.
int fvm_allocate_partition(int fvm_fd, const alloc_req_t* request);

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
int open_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID,
                   zx_duration_t timeout, char* out_path);

// Finds and destroys the partition with the given GUID pair, if it exists.
zx_status_t destroy_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID);

__END_CDECLS
