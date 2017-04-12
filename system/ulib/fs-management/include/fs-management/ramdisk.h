// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdlib.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS

// Creates a ramdisk named "ramdisk_name", and returns the full path to the
// ramdisk in ramdisk_path_out. This path should be at least PATH_MAX
// characters long.
//
// Since "ramdisk_name" will be transformed into a device name, its maximum
// length is limited by the maximum device name (MX_DEVICE_NAME_MAX) defined
// in the ddk. Additionally, to make ramdisk-dependent tests safe for
// multi-process environments, a zero-padded process koid is appended to the
// provided ramdisk name.
//
// Due to these constraints, it is recommended that "ramdisk_name" be short,
// on the order of 14 characters or less (disclaimer: This length is subject
// to change if the ddk name maximum changes, and if a name is too long, an
// error will be thrown explicitly, rather than resulting in undefined
// behavior).
//
// Return 0 on success, -1 on error.
int create_ramdisk(const char* ramdisk_name, char* ramdisk_path_out,
                   uint64_t blk_size, uint64_t blk_count);

// Destroys a ramdisk, given the "ramdisk_path" returned from "create_ramdisk".
//
// Return 0 on success, -1 on error.
int destroy_ramdisk(const char* ramdisk_path);

__END_CDECLS
