// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unittest/unittest.h>

#include "misc.h"
#include "filesystems.h"

bool fcheck_dir_contents(DIR* dir, expected_dirent_t* edirents, size_t len) {
    rewinddir(dir);
    size_t seen = 0;
    while (seen != len) {
        struct dirent* de = readdir(dir);
        ASSERT_NEQ(de, NULL, "Didn't see all expected direntries");
        bool found = false;
        for (size_t i = 0; i < len; i++) {
            if (strcmp(edirents[i].d_name, de->d_name) == 0) {
                ASSERT_EQ(edirents[i].d_type, de->d_type, "Saw direntry with unexpected type");
                ASSERT_FALSE(edirents[i].seen, "Direntry seen twice");
                edirents[i].seen = true;
                seen++;
                found = true;
                break;
            }
        }

        ASSERT_TRUE(found, "Saw an unexptected dirent");
    }

    ASSERT_EQ(readdir(dir), NULL, "There exists an entry we didn't expect to see");

    // Flip 'seen' back to false so the array of expected dirents can be reused
    for (size_t i = 0; i < len; i++) {
        edirents[i].seen = false;
    }
    return true;
}

bool check_dir_contents(const char* dirname, expected_dirent_t* edirents, size_t len) {
    DIR* dir = opendir(dirname);
    bool ret = fcheck_dir_contents(dir, edirents, len);
    ASSERT_EQ(closedir(dir), 0, "Couldn't close inspected directory");
    return ret;
}

// Check the contents of a file are what we expect
bool check_file_contents(int fd, const uint8_t* buf, size_t length) {
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    uint8_t* out = malloc(length);
    ASSERT_NEQ(out, NULL, "Failed to allocate checking buffer");
    ASSERT_STREAM_ALL(read, fd, out, length);
    ASSERT_EQ(memcmp(buf, out, length), 0, "");
    free(out);
    return true;
}

bool check_remount(void) {
    ASSERT_EQ(test_info->unmount(test_root_path), 0, "");
    ASSERT_EQ(test_info->fsck(test_disk_path), 0, "");
    ASSERT_EQ(test_info->mount(test_disk_path, test_root_path), 0, "");
    return true;
}
