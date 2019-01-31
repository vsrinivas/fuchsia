// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <zircon/compiler.h>

// Filesystem test utilities

__BEGIN_CDECLS;

#define ASSERT_STREAM_ALL(op, fd, buf, len) \
    ASSERT_EQ(op(fd, (buf), (len)), (ssize_t)(len), "");

typedef struct expected_dirent {
    bool seen; // Should be set to "false", used internally by checking function.
    const char* d_name;
    unsigned char d_type;
} expected_dirent_t;

bool fcheck_dir_contents(DIR* dir, expected_dirent_t* edirents, size_t len);
bool check_dir_contents(const char* dirname, expected_dirent_t* edirents, size_t len);

// Check the contents of a file are what we expect
bool check_file_contents(int fd, const uint8_t* buf, size_t length);

// Unmount and remount our filesystem, simulating a reboot
bool check_remount(void);

__END_CDECLS;
