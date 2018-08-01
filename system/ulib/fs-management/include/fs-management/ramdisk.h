// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdlib.h>

#include <zircon/compiler.h>
#include <zircon/device/ramdisk.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Wait for a device at "path" to become available.
//
// Returns ZX_OK if the device is ready to be opened, or ZX_ERR_TIMED_OUT if
// the device is not available after "timeout" has elapsed.
int wait_for_device(const char* path, zx_duration_t timeout);

// Creates a ramdisk  returns the full path to the ramdisk in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
//
// Return 0 on success, -1 on error.
int create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path);

// Creates a ramdisk  returns the full path to the ramdisk in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
//
// Return 0 on success, -1 on error.
int create_ramdisk_with_guid(uint64_t blk_size, uint64_t blk_count, const uint8_t* type_guid,
                             size_t guid_len, char* out_path);

// Same but uses an existing VMO as the ramdisk.
// The handle is always consumed, and must be the only handle to this VMO.
int create_ramdisk_from_vmo(zx_handle_t vmo, char* out_path);

// Puts the ramdisk at |ramdisk_path| to sleep after |blk_count| blocks written.
// After this, transactions will no longer be immediately persisted to disk.
// If the |RAMDISK_FLAG_RESUME_ON_WAKE| flag has been set, transactions will
// be processed when |wake_ramdisk| is called, otherwise they will fail immediately.
int sleep_ramdisk(const char* ramdisk_path, uint64_t blk_count);

// Wake the ramdisk at |ramdisk_path| from a sleep state.
int wake_ramdisk(const char* ramdisk_path);

// Returns the ramdisk's current failed, successful, and total block counts as |counts|.
int get_ramdisk_blocks(const char* ramdisk_path, ramdisk_blk_counts_t* counts);

// Destroys a ramdisk, given the "ramdisk_path" returned from "create_ramdisk".
//
// Return 0 on success, -1 on error.
int destroy_ramdisk(const char* ramdisk_path);

__END_CDECLS
