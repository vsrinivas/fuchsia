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

// Wait for a "parent" device to have a child "driver" bound.
//
// Return 0 on success, -1 on error.
int wait_for_driver_bind(const char* parent, const char* driver);

// Creates a ramdisk  returns the full path to the ramdisk in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
//
// Return 0 on success, -1 on error.
int create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path);

// Same but uses an existing VMO as the ramdisk.
// The handle is always consumed, and must be the only handle to this VMO.
int create_ramdisk_from_vmo(zx_handle_t vmo, char* out_path);

// Puts the ramdisk at |ramdisk_path| to sleep after |txn_count| transactions.
// After this, transactions will no longer be immediately persisted to disk.
// If the |RAMDISK_FLAG_RESUME_ON_WAKE| flag has been set, transactions will
// be processed when |wake_ramdisk| is called, otherwise they will fail immediately.
int sleep_ramdisk(const char* ramdisk_path, uint64_t txn_count);

// Wake the ramdisk at |ramdisk_path| from a sleep state.
int wake_ramdisk(const char* ramdisk_path);

// Returns the transactions |counts| for the ramdisk at |ramdisk_path|.
int get_ramdisk_txns(const char* ramdisk_path, ramdisk_txn_counts_t *counts);

// Destroys a ramdisk, given the "ramdisk_path" returned from "create_ramdisk".
//
// Return 0 on success, -1 on error.
int destroy_ramdisk(const char* ramdisk_path);

__END_CDECLS
