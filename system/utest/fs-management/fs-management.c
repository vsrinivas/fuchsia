// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <magenta/device/block.h>
#include <magenta/device/devmgr.h>
#include <magenta/device/ramdisk.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>

static bool check_mounted_fs(const char* path, const char* fs_name, size_t len) {
    int fd = open(path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    char out[128];
    ASSERT_EQ(ioctl_devmgr_query_fs(fd, out, sizeof(out)), (ssize_t)len,
              "Failed to query filesystem");
    ASSERT_EQ(strncmp(fs_name, out, len), 0, "Unexpected filesystem mounted");
    ASSERT_EQ(close(fd), 0, "");
    return true;
}

static bool mount_unmount(void) {
    const char* ramdisk_name = "mount_unmount";
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(ramdisk_name, ramdisk_path, 512, 1 << 16), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              NO_ERROR, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "minfs", strlen("minfs")), "");
    ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_remount(void) {
    const char* ramdisk_name = "mount_remount";
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_remount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(ramdisk_name, ramdisk_path, 512, 1 << 16), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");

    // We should still be able to mount and unmount the filesystem multiple times
    for (size_t i = 0; i < 10; i++) {
        int fd = open(ramdisk_path, O_RDWR);
        ASSERT_GE(fd, 0, "");
        ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async),
                  NO_ERROR, "");
        ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    }
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_fsck(void) {
    const char* ramdisk_name = "mount_fsck";
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_fsck";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(ramdisk_name, ramdisk_path, 512, 1 << 16), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              NO_ERROR, "");
    ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    // fsck shouldn't require any user input for a newly mkfs'd filesystem
    ASSERT_EQ(fsck(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

BEGIN_TEST_CASE(fs_management_tests)
RUN_TEST_MEDIUM(mount_unmount)
RUN_TEST_MEDIUM(mount_remount)
RUN_TEST_MEDIUM(mount_fsck)
END_TEST_CASE(fs_management_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
