// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct fs_info {
    const char* name;
    int (*mkfs)(const char* disk_path);
    int (*mount)(const char* disk_path, const char* mount_path);
    int (*unmount)(const char* mount_path);
    bool can_be_mounted;
    bool can_mount_sub_filesystems;
    bool supports_hardlinks;
} fs_info_t;

// Path to mounted filesystem currently being tested
extern const char* test_root_path;
// Path to the mounted filesystem's backing store (if it exists)
extern char test_disk_path[];

#define NUM_FILESYSTEMS 2
extern fs_info_t FILESYSTEMS[NUM_FILESYSTEMS];

int create_ramdisk(const char* ramdisk_name, char* ramdisk_path_out);
int destroy_ramdisk(const char* ramdisk_path);
