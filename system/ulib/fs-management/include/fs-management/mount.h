// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/types.h>

typedef enum disk_format_type {
    DISK_FORMAT_UNKNOWN,
    DISK_FORMAT_GPT,
    DISK_FORMAT_MINFS,
    DISK_FORMAT_FAT,
} disk_format_t;

disk_format_t detect_disk_format(int fd);

typedef struct mount_options {
    bool readonly;
    bool verbose_mount;
} mount_options_t;

static const mount_options_t default_mount_options = {
    .readonly = false,
    .verbose_mount = false,
};

// Given the following:
//  - A device containing a filesystem image of a known format
//  - A path on which to mount the filesystem
//  - Some configuration options for launching the filesystem, and
//  - A callback which can be used to 'launch' an fs server,
//
// Prepare the argv arguments to the filesystem process, mount a handle on the
// expected mountpath, and call the 'launch' callback (if the filesystem is
// recognized).
mx_status_t mount(const char* devicepath, const char* mountpath, disk_format_t df,
                  const mount_options_t* options,
                  mx_status_t (*cb)(int argc, const char** argv, mx_handle_t h));

// Format the provided device with a requested disk format.
mx_status_t mkfs(const char* devicepath, disk_format_t df,
                 mx_status_t (*cb)(int argc, const char** argv));

// Umount the filesystem process.
//
// Returns ERR_BAD_STATE if mountpath could not be opened.
// Returns ERR_NOT_FOUND if there is no mounted filesystem on mountpath.
// Other errors may also be returned if problems occur while unmounting.
mx_status_t umount(const char* mountpath);
