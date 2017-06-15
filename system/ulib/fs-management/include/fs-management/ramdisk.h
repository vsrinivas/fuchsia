// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdlib.h>

#include <magenta/compiler.h>

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

// Destroys a ramdisk, given the "ramdisk_path" returned from "create_ramdisk".
//
// Return 0 on success, -1 on error.
int destroy_ramdisk(const char* ramdisk_path);

__END_CDECLS
