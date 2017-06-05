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
#include <magenta/device/vfs.h>
#include <magenta/device/ramdisk.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>

static bool check_mounted_fs(const char* path, const char* fs_name, size_t len) {
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0, "");
    vfs_query_info_t out;
    ASSERT_EQ(ioctl_vfs_query_fs(fd, &out, sizeof(out)), (ssize_t)sizeof(out), "Failed to query filesystem");
    ASSERT_EQ(strncmp(fs_name, out.name, len), 0, "Unexpected filesystem mounted");
    ASSERT_LE(out.used_nodes, out.total_nodes, "Used nodes greater than free nodes");
    ASSERT_LE(out.used_bytes, out.total_bytes, "Used bytes greater than free bytes");
    //TODO(planders): eventually check that total/used counts are > 0
    ASSERT_EQ(close(fd), 0, "");
    return true;
}

static bool mount_unmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "minfs", strlen("minfs")), "");
    ASSERT_EQ(umount(mount_path), MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_mkdir_unmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_mkdir_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    mount_options_t options = default_mount_options;
    options.create_mountpoint = true;
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &options,
                    launch_stdio_async),
              MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "minfs", strlen("minfs")), "");
    ASSERT_EQ(umount(mount_path), MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool fmount_funmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");

    int mountfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "minfs", strlen("minfs")), "");
    ASSERT_EQ(fumount(mountfd), MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "memfs", strlen("memfs")), "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(close(mountfd), 0, "Couldn't close ex-mount point");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

// All "parent" filesystems attempt to mount a MinFS ramdisk under malicious
// conditions.
bool do_mount_evil(const char* parentfs_name, const char* mount_path) {
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");

    int mountfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");

    // Everything *would* be perfect to call fmount, when suddenly...
    ASSERT_EQ(rmdir(mount_path), 0, "");
    // The directory was unlinked! We can't mount now!
    ASSERT_NEQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
               MX_OK, "");
    ASSERT_NEQ(fumount(mountfd), MX_OK, "");
    ASSERT_EQ(close(mountfd), 0, "Couldn't close unlinked not-mount point");

    // Re-acquire the ramdisk mount point; it's always consumed...
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");

    // Okay, okay, let's get a new mount path...
    mountfd = open(mount_path, O_CREAT | O_RDWR);
    ASSERT_GT(mountfd, 0, "");
    // Wait a sec, that was a file, not a directory! We can't mount that!
    ASSERT_NEQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
               MX_OK, "");
    ASSERT_NEQ(fumount(mountfd), MX_OK, "");
    ASSERT_EQ(close(mountfd), 0, "Couldn't close file not-mount point");
    ASSERT_EQ(unlink(mount_path), 0, "");

    // Okay, fine, let's mount successfully...
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    mountfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");
    // Awesome, that worked. But we shouldn't be able to mount again!
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    ASSERT_NEQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
               MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, "minfs", strlen("minfs")), "");
    ASSERT_EQ(fumount(mountfd), MX_OK, "");
    ASSERT_TRUE(check_mounted_fs(mount_path, parentfs_name, strlen(parentfs_name)), "");
    ASSERT_EQ(close(mountfd), 0, "");
    ASSERT_EQ(rmdir(mount_path), 0, "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    return true;
}

static bool mount_evil_memfs(void) {
    BEGIN_TEST;
    const char* mount_path = "/tmp/mount_evil";
    ASSERT_TRUE(do_mount_evil("memfs", mount_path), "");
    END_TEST;
}

static bool mount_evil_minfs(void) {
    char ramdisk_path[PATH_MAX];

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    const char* parent_path = "/tmp/parent";
    ASSERT_EQ(mkdir(parent_path, 0666), 0, "");
    int mountfd = open(parent_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    int ramdiskfd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdiskfd, 0, "");
    ASSERT_EQ(fmount(ramdiskfd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");
    ASSERT_EQ(close(mountfd), 0, "");

    const char* mount_path = "/tmp/parent/mount_evil";
    ASSERT_TRUE(do_mount_evil("minfs", mount_path), "");

    ASSERT_EQ(umount(parent_path), 0, "");
    ASSERT_EQ(rmdir(parent_path), 0, "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    END_TEST;
}

static bool mount_remount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_remount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");

    // We should still be able to mount and unmount the filesystem multiple times
    for (size_t i = 0; i < 10; i++) {
        int fd = open(ramdisk_path, O_RDWR);
        ASSERT_GE(fd, 0, "");
        ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async),
                  MX_OK, "");
        ASSERT_EQ(umount(mount_path), MX_OK, "");
    }
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

static bool mount_fsck(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_fsck";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0, "");
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), MX_OK, "");
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");
    ASSERT_EQ(umount(mount_path), MX_OK, "");
    // fsck shouldn't require any user input for a newly mkfs'd filesystem
    ASSERT_EQ(fsck(ramdisk_path, DISK_FORMAT_MINFS, &default_fsck_options, launch_stdio_sync),
              MX_OK, "");
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0, "");
    ASSERT_EQ(unlink(mount_path), 0, "");
    END_TEST;
}

BEGIN_TEST_CASE(fs_management_tests)
RUN_TEST_MEDIUM(mount_unmount)
RUN_TEST_MEDIUM(mount_mkdir_unmount)
RUN_TEST_MEDIUM(fmount_funmount)
RUN_TEST_MEDIUM(mount_evil_memfs)
RUN_TEST_MEDIUM(mount_evil_minfs)
RUN_TEST_MEDIUM(mount_remount)
RUN_TEST_MEDIUM(mount_fsck)
END_TEST_CASE(fs_management_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
