// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "util.h"

#include <minfs/fsck.h>

void setup_fs_test(size_t disk_size) {
    int r = open(MOUNT_PATH, O_RDWR | O_CREAT | O_EXCL, 0755);

    if (r < 0) {
        fprintf(stderr, "Unable to create disk for test filesystem\n");
        exit(-1);
    }

    if (ftruncate(r, disk_size) < 0) {
        fprintf(stderr, "Unable to truncate disk\n");
        exit(-1);
    }

    if (close(r) < 0) {
        fprintf(stderr, "Unable to close disk\n");
        exit(-1);
    }

    if (emu_mkfs(MOUNT_PATH) < 0) {
        fprintf(stderr, "Unable to run mkfs\n");
        exit(-1);
    }

    if (emu_mount(MOUNT_PATH) < 0) {
        fprintf(stderr, "Unable to run mount\n");
        exit(-1);
    }
}

void teardown_fs_test() {
    if (unlink(MOUNT_PATH) < 0) {
        exit(-1);
    }
}

int run_fsck() {
    fbl::unique_fd disk(open(MOUNT_PATH, O_RDONLY));

    if (!disk) {
        fprintf(stderr, "Unable to open disk for fsck\n");
        return -1;
    }

    struct stat stats;
    if (fstat(disk.get(), &stats) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return 0;
    }

    if (stats.st_size == 0) {
        fprintf(stderr, "Invalid disk\n");
        return -1;
    }

    size_t size = stats.st_size /= minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> block_cache;
    if (minfs::Bcache::Create(&block_cache, fbl::move(disk), size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    return minfs_check(fbl::move(block_cache));
}
