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
#include <magenta/device/ramdisk.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include <fs-management/mount.h>

#define RAMCTL_PATH "/dev/misc/ramctl"

static int set_up_ramdisk(const char* name, uint64_t blk_size, uint64_t blk_count) {
    // Open the "ramdisk controller", and ask it to create a ramdisk for us.
    int fd = open(RAMCTL_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramctl device");
    ramdisk_ioctl_config_t config;
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    strcpy(config.name, name);
    ssize_t r = ioctl_ramdisk_config(fd, &config);
    ASSERT_EQ(r, NO_ERROR, "Failed to create ramdisk");
    ASSERT_EQ(close(fd), 0, "Failed to close ramctl");

    // TODO(smklein): This "sleep" prevents a bug from triggering:
    // - 'ioctl_ramdisk_config' --> 'device_add' --> 'open' *should* work, but sometimes
    //   fails, as the ramdisk does not exist in the FS heirarchy yet. (MG-468)
    usleep(10000);

    // At this point, our ramdisk is accessible from filesystem hierarchy
    char ramdisk_path[PATH_MAX];
    snprintf(ramdisk_path, sizeof(ramdisk_path), "%s/%s", RAMCTL_PATH, name);
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    return fd;
}

static int shut_down_ramdisk(const char* name) {
    char ramdisk_path[PATH_MAX];
    snprintf(ramdisk_path, sizeof(ramdisk_path), "%s/%s", RAMCTL_PATH, name);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0, "");
    return 0;
}

static bool mount_unmount(void) {
    const char* ramdisk_name = "mount_unmount";
    const char* ramdisk_path = RAMCTL_PATH "/mount_unmount";
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    int fd = set_up_ramdisk(ramdisk_name, 512, 1 << 16);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              NO_ERROR, "");
    ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    ASSERT_EQ(shut_down_ramdisk(ramdisk_name), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_remount(void) {
    const char* ramdisk_name = "mount_remount";
    const char* ramdisk_path = RAMCTL_PATH "/mount_remount";
    const char* mount_path = "/tmp/mount_remount";

    BEGIN_TEST;
    int fd = set_up_ramdisk(ramdisk_name, 512, 1 << 16);
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");

    // We should still be able to mount and unmount the filesystem multiple times
    for (size_t i = 0; i < 10; i++) {
        fd = open(ramdisk_path, O_RDWR);
        ASSERT_GE(fd, 0, "");
        ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async),
                  NO_ERROR, "");
        ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    }
    ASSERT_EQ(shut_down_ramdisk(ramdisk_name), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_fsck(void) {
    const char* ramdisk_name = "mount_fsck";
    const char* ramdisk_path = RAMCTL_PATH "/mount_fsck";
    const char* mount_path = "/tmp/mount_fsck";

    BEGIN_TEST;
    int fd = set_up_ramdisk(ramdisk_name, 512, 1 << 16);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              NO_ERROR, "");
    ASSERT_EQ(umount(mount_path), NO_ERROR, "");
    // fsck shouldn't require any user input for a newly mkfs'd filesystem
    ASSERT_EQ(fsck(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync), NO_ERROR, "");
    ASSERT_EQ(shut_down_ramdisk(ramdisk_name), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

BEGIN_TEST_CASE(fs_management_tests)
RUN_TEST(mount_unmount)
RUN_TEST(mount_remount)
RUN_TEST(mount_fsck)
END_TEST_CASE(fs_management_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
