// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>

namespace {

typedef union {
    vfs_query_info_t info;
    struct {
        alignas(vfs_query_info_t) char h[sizeof(vfs_query_info_t)];
        char name[MAX_FS_NAME_LEN];
    };
} vfs_query_info_wrapper_t;

bool CheckMountedFs(const char* path, const char* fs_name, size_t len) {
    BEGIN_HELPER;
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    vfs_query_info_wrapper_t wrapper;
    ssize_t r = ioctl_vfs_query_fs(fd, &wrapper.info, sizeof(wrapper) - 1);
    ASSERT_EQ(r, (ssize_t)(sizeof(vfs_query_info_t) + len), "Failed to query filesystem");
    wrapper.name[r - sizeof(vfs_query_info_t)] = '\0';
    ASSERT_EQ(strncmp(fs_name, wrapper.name, len), 0, "Unexpected filesystem mounted");
    ASSERT_LE(wrapper.info.used_nodes, wrapper.info.total_nodes,
              "Used nodes greater than free nodes");
    ASSERT_LE(wrapper.info.used_bytes, wrapper.info.total_bytes,
              "Used bytes greater than free bytes");
    //TODO(planders): eventually check that total/used counts are > 0
    ASSERT_EQ(close(fd), 0);
    END_HELPER;
}

bool MountUnmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));
    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool MountMkdirUnmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_mkdir_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    mount_options_t options = default_mount_options;
    options.create_mountpoint = true;
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));
    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool FmountFunmount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);

    int mountfd = open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));
    ASSERT_EQ(fumount(mountfd), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(close(mountfd), 0, "Couldn't close ex-mount point");
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

// All "parent" filesystems attempt to mount a MinFS ramdisk under malicious
// conditions.
//
// Note: For cases where "fmount" fails, we briefly sleep to allow the
// filesystem to unmount itself and relinquish control of the block device.
bool DoMountEvil(const char* parentfs_name, const char* mount_path) {
    BEGIN_HELPER;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);

    int mountfd = open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");

    // Everything *would* be perfect to call fmount, when suddenly...
    ASSERT_EQ(rmdir(mount_path), 0);
    // The directory was unlinked! We can't mount now!
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_ERR_NOT_DIR);
    usleep(10000);
    ASSERT_NE(fumount(mountfd), ZX_OK);
    ASSERT_EQ(close(mountfd), 0, "Couldn't close unlinked not-mount point");

    // Re-acquire the ramdisk mount point; it's always consumed...
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);

    // Okay, okay, let's get a new mount path...
    mountfd = open(mount_path, O_CREAT | O_RDWR);
    ASSERT_GT(mountfd, 0);
    // Wait a sec, that was a file, not a directory! We can't mount that!
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_ERR_ACCESS_DENIED);
    usleep(10000);
    ASSERT_NE(fumount(mountfd), ZX_OK);
    ASSERT_EQ(close(mountfd), 0, "Couldn't close file not-mount point");
    ASSERT_EQ(unlink(mount_path), 0);

    // Re-acquire the ramdisk mount point again...
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    // Try mounting without O_ADMIN (which is disallowed)
    mountfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_ERR_ACCESS_DENIED);
    usleep(10000);
    ASSERT_EQ(close(mountfd), 0, "Couldn't close the unpriviledged mount point");

    // Okay, fine, let's mount successfully...
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    mountfd = open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    // Awesome, that worked. But we shouldn't be able to mount again!
    fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fmount(fd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_ERR_BAD_STATE);
    usleep(10000);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));

    // Let's try removing the mount point (we shouldn't be allowed to do so)
    ASSERT_EQ(rmdir(mount_path), -1);
    ASSERT_EQ(errno, EBUSY);

    // Let's try telling the target filesystem to shut down
    // WITHOUT O_ADMIN
    int badfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(badfd, 0);
    ASSERT_EQ(ioctl_vfs_unmount_fs(badfd), ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(close(badfd), 0);

    // Let's try unmounting the filesystem WITHOUT O_ADMIN
    // (unpinning the remote handle from the parent FS).
    badfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    zx_handle_t h;
    ASSERT_EQ(ioctl_vfs_unmount_node(badfd, &h), ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(close(badfd), 0);

    // When we unmount with an O_ADMIN handle, it should successfully detach.
    ASSERT_EQ(fumount(mountfd), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, parentfs_name, strlen(parentfs_name)));
    ASSERT_EQ(close(mountfd), 0);
    ASSERT_EQ(rmdir(mount_path), 0);
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    END_HELPER;
}

bool MountEvilMemfs(void) {
    BEGIN_TEST;
    const char* mount_path = "/tmp/mount_evil";
    ASSERT_TRUE(DoMountEvil("memfs", mount_path));
    END_TEST;
}

bool MountEvilMinfs(void) {
    char ramdisk_path[PATH_MAX];

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    const char* parent_path = "/tmp/parent";
    ASSERT_EQ(mkdir(parent_path, 0666), 0);
    int mountfd = open(parent_path, O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    int ramdiskfd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdiskfd, 0);
    ASSERT_EQ(fmount(ramdiskfd, mountfd, DISK_FORMAT_MINFS, &default_mount_options,
                     launch_stdio_async),
              ZX_OK);
    ASSERT_EQ(close(mountfd), 0);

    const char* mount_path = "/tmp/parent/mount_evil";
    ASSERT_TRUE(DoMountEvil("minfs", mount_path));

    ASSERT_EQ(umount(parent_path), 0);
    ASSERT_EQ(rmdir(parent_path), 0);
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    END_TEST;
}

bool UmountTestEvil(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/umount_test_evil";

    BEGIN_TEST;

    // Create a ramdisk, mount minfs
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));

    // Try re-opening the root without O_ADMIN. We shouldn't be able to umount.
    int weak_root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(weak_root_fd, 0);
    ASSERT_LT(ioctl_vfs_unmount_fs(weak_root_fd), 0);

    // Try opening a non-root directory without O_ADMIN. We shouldn't be able
    // to umount.
    ASSERT_EQ(mkdirat(weak_root_fd, "subdir", 0666), 0);
    int weak_subdir_fd = openat(weak_root_fd, "subdir", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(weak_subdir_fd, 0);
    ASSERT_LT(ioctl_vfs_unmount_fs(weak_subdir_fd), 0);
    ASSERT_EQ(close(weak_subdir_fd), 0);

    // Try opening a new directory with O_ADMIN. It shouldn't open.
    weak_subdir_fd = openat(weak_root_fd, "subdir", O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_LT(weak_subdir_fd, 0);
    ASSERT_EQ(close(weak_root_fd), 0);

    // Finally, umount using O_NOREMOTE and acquiring the connection
    // that has "O_ADMIN" set.
    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool DoubleMountRoot(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/double_mount_root";

    BEGIN_TEST;

    // Create a ramdisk, mount minfs
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));

    // Create ANOTHER ramdisk, ready to be mounted...
    // Try mounting again on top Minfs' remote root.
    char ramdisk_path2[PATH_MAX];
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path2), 0);
    ASSERT_EQ(mkfs(ramdisk_path2, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);

    // Try mounting on the mount point (locally; should fail because something is already mounted)
    int mount_fd = open(mount_path, O_RDONLY | O_NOREMOTE | O_ADMIN);
    ASSERT_GE(mount_fd, 0);
    fd = open(ramdisk_path2, O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_NE(fmount(fd, mount_fd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_EQ(close(mount_fd), 0);

    // Try mounting on the mount root (remote; should fail because MinFS doesn't allow mounting
    // on top of the root directory).
    mount_fd = open(mount_path, O_RDONLY | O_ADMIN);
    ASSERT_GE(mount_fd, 0);
    fd = open(ramdisk_path2, O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_NE(fmount(fd, mount_fd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_EQ(close(mount_fd), 0);


    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(destroy_ramdisk(ramdisk_path2), 0);
    ASSERT_EQ(rmdir(mount_path), 0);
    END_TEST;
}

bool MountRemount(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_remount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);

    // We should still be able to mount and unmount the filesystem multiple times
    for (size_t i = 0; i < 10; i++) {
        int fd = open(ramdisk_path, O_RDWR);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async),
                  ZX_OK);
        ASSERT_EQ(umount(mount_path), ZX_OK);
    }
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool MountFsck(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_fsck";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_EQ(umount(mount_path), ZX_OK);
    // fsck shouldn't require any user input for a newly mkfs'd filesystem
    ASSERT_EQ(fsck(ramdisk_path, DISK_FORMAT_MINFS, &default_fsck_options, launch_stdio_sync),
              ZX_OK);
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool MountGetDevice(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_get_device";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));

    int mountfd = open(mount_path, O_RDONLY | O_ADMIN);
    ASSERT_GT(mountfd, 0);
    char device_path[1024];
    ssize_t path_len = ioctl_vfs_get_device_path(mountfd, device_path, sizeof(device_path));
    ASSERT_LT(path_len, 0);
    ASSERT_EQ(close(mountfd), 0);

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));

    mountfd = open(mount_path, O_RDONLY | O_ADMIN);
    ASSERT_GT(mountfd, 0);
    path_len = ioctl_vfs_get_device_path(mountfd, device_path, sizeof(device_path));
    ASSERT_GT(path_len, 0, "Device path not found");
    ASSERT_EQ(strncmp(ramdisk_path, device_path, path_len), 0, "Unexpected device path");
    ASSERT_EQ(close(mountfd), 0);

    mountfd = open(mount_path, O_RDONLY);
    ASSERT_GT(mountfd, 0);
    path_len = ioctl_vfs_get_device_path(mountfd, device_path, sizeof(device_path));
    ASSERT_LT(path_len, 0);
    ASSERT_EQ(close(mountfd), 0);

    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));

    mountfd = open(mount_path, O_RDONLY | O_ADMIN);
    ASSERT_GT(mountfd, 0);
    path_len = ioctl_vfs_get_device_path(fd, device_path, sizeof(device_path));
    ASSERT_LT(path_len, 0);
    ASSERT_EQ(close(mountfd), 0);

    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

// Mounts a minfs formatted partition to the desired point.
bool MountMinfs(int block_fd, bool read_only, const char* mount_path) {
    BEGIN_HELPER;
    mount_options_t options;
    memcpy(&options, &default_mount_options, sizeof(mount_options_t));
    options.readonly = read_only;

    ASSERT_EQ(mount(block_fd, mount_path, DISK_FORMAT_MINFS, &options, launch_stdio_async), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));
    END_HELPER;
}

// Formats the ramdisk with minfs, and writes a small file to it.
bool CreateTestFile(const char* ramdisk_path, const char* mount_path, const char* file_name) {
    BEGIN_HELPER;
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_TRUE(MountMinfs(fd, false, mount_path));

    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    fd = openat(root_fd, file_name, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(write(fd, "hello", 6), 6);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(root_fd), 0);
    ASSERT_EQ(umount(mount_path), ZX_OK);
    END_HELPER;
}

// Tests that setting read-only on the mount options works as expected.
bool MountReadonly(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_readonly";
    const char file_name[] = "some_file";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_TRUE(CreateTestFile(ramdisk_path, mount_path, file_name));

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);

    bool read_only = true;
    ASSERT_TRUE(MountMinfs(fd, read_only, mount_path));

    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    fd = openat(root_fd, file_name, O_CREAT | O_RDWR);

    // We can no longer open the file as writable
    ASSERT_LT(fd, 0);

    // We CAN open it as readable though
    fd = openat(root_fd, file_name, O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_LT(write(fd, "hello", 6), 0);
    char buf[6];
    ASSERT_EQ(read(fd, buf, 6), 6);
    ASSERT_EQ(memcmp(buf, "hello", 6), 0);

    ASSERT_LT(renameat(root_fd, file_name, root_fd, "new_file"), 0);
    ASSERT_LT(unlinkat(root_fd, file_name, 0), 0);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(root_fd), 0);
    ASSERT_EQ(umount(mount_path), ZX_OK);

    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);

    END_TEST;
}

// Test that when a block device claims to be read-only, the filesystem is mounted as read-only.
bool MountBlockReadonly(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_readonly";
    const char file_name[] = "some_file";

    BEGIN_TEST;

    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_TRUE(CreateTestFile(ramdisk_path, mount_path, file_name));

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);

    uint32_t flags = BLOCK_FLAG_READONLY;
    ASSERT_EQ(0, ioctl_ramdisk_set_flags(fd, &flags));

    bool read_only = false;
    ASSERT_TRUE(MountMinfs(fd, read_only, mount_path));

    // We can't modify the file.
    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    fd = openat(root_fd, file_name, O_CREAT | O_RDWR);
    ASSERT_LT(fd, 0);

    // We can open it as read-only.
    fd = openat(root_fd, file_name, O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(root_fd), 0);
    ASSERT_EQ(umount(mount_path), ZX_OK);

    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);

    END_TEST;
}

bool StatfsTest(void) {
    char ramdisk_path[PATH_MAX];
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ASSERT_EQ(create_ramdisk(512, 1 << 16, ramdisk_path), 0);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);

    struct statfs stats;
    int rc = statfs("", &stats);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    rc = statfs(mount_path, &stats);
    ASSERT_EQ(rc, 0);

    // Verify that at least some values make sense, without making the test too brittle.
    ASSERT_EQ(stats.f_type, VFS_TYPE_MINFS);
    ASSERT_NE(stats.f_fsid.__val[0] | stats.f_fsid.__val[1], 0);
    ASSERT_EQ(stats.f_bsize, 8192u);
    ASSERT_EQ(stats.f_namelen, 255u);
    ASSERT_GT(stats.f_bavail, 0u);
    ASSERT_GT(stats.f_ffree, 0u);

    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_EQ(destroy_ramdisk(ramdisk_path), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(fs_management_tests)
RUN_TEST_MEDIUM(MountUnmount)
RUN_TEST_MEDIUM(MountMkdirUnmount)
RUN_TEST_MEDIUM(FmountFunmount)
RUN_TEST_MEDIUM(MountEvilMemfs)
RUN_TEST_MEDIUM(MountEvilMinfs)
RUN_TEST_MEDIUM(UmountTestEvil)
RUN_TEST_MEDIUM(DoubleMountRoot)
RUN_TEST_MEDIUM(MountRemount)
RUN_TEST_MEDIUM(MountFsck)
RUN_TEST_MEDIUM(MountGetDevice)
RUN_TEST_MEDIUM(MountReadonly)
RUN_TEST_MEDIUM(MountBlockReadonly)
RUN_TEST_MEDIUM(StatfsTest)
END_TEST_CASE(fs_management_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
