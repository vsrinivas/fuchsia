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
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-test-utils/fixture.h>
#include <fs-test-utils/unittest.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fs-management/mount.h>
#include <ramdevice-client/ramdisk.h>

#include <utility>

namespace {

fs_test_utils::FixtureOptions PartitionOverFvmWithRamdisk() {
    fs_test_utils::FixtureOptions options =
        fs_test_utils::FixtureOptions::Default(DISK_FORMAT_MINFS);
    options.use_fvm = true;
    options.fs_format = false;
    options.fs_mount = false;
    return options;
}

fs_test_utils::FixtureOptions MinfsRamdiskOptions() {
    fs_test_utils::FixtureOptions options =
        fs_test_utils::FixtureOptions::Default(DISK_FORMAT_MINFS);
    options.use_fvm = false;
    options.fs_format = true;
    options.fs_mount = true;
    return options;
}

bool CheckMountedFs(const char* path, const char* fs_name, size_t len) {
    BEGIN_HELPER;
    fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd);

    fuchsia_io_FilesystemInfo info;
    zx_status_t status;
    fzl::FdioCaller caller(std::move(fd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &info),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(strncmp(fs_name, reinterpret_cast<char*>(info.name), strlen(fs_name)), 0);
    ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
    ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
    // TODO(planders): eventually check that total/used counts are > 0
    END_HELPER;
}

bool MountUnmountShared(size_t block_size) {
    BEGIN_HELPER;

    ramdisk_client_t* ramdisk = nullptr;
    const char* mount_path = "/tmp/mount_unmount";

    ASSERT_EQ(ramdisk_create(block_size, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_HELPER;
}

bool MountUnmount() {
    BEGIN_TEST;
    ASSERT_TRUE(MountUnmountShared(512));
    END_TEST;
}

bool MountUnmountLargeBlock() {
    BEGIN_TEST;
    ASSERT_TRUE(MountUnmountShared(8192));
    END_TEST;
}

bool MountMkdirUnmount() {
    const char* mount_path = "/tmp/mount_mkdir_unmount";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    mount_options_t options = default_mount_options;
    options.create_mountpoint = true;
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &options, launch_stdio_async), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));
    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool FmountFunmount() {
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
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
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    fbl::unique_fd badfd(open(mount_path, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(badfd);
    zx_status_t status;
    fzl::FdioCaller caller(std::move(badfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminUnmount(caller.borrow_channel(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(close(caller.release().release()), 0);

    // Let's try unmounting the filesystem WITHOUT O_ADMIN
    // (unpinning the remote handle from the parent FS).
    badfd.reset(open(mount_path, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(badfd);
    zx_handle_t h;
    caller.reset(std::move(badfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminUnmountNode(caller.borrow_channel(), &status, &h), ZX_OK);
    ASSERT_EQ(h, ZX_HANDLE_INVALID);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(close(caller.release().release()), 0);

    // When we unmount with an O_ADMIN handle, it should successfully detach.
    ASSERT_EQ(fumount(mountfd), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, parentfs_name, strlen(parentfs_name)));
    ASSERT_EQ(close(mountfd), 0);
    ASSERT_EQ(rmdir(mount_path), 0);
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    END_HELPER;
}

bool MountEvilMemfs() {
    BEGIN_TEST;
    const char* mount_path = "/tmp/mount_evil";
    ASSERT_TRUE(DoMountEvil("memfs", mount_path));
    END_TEST;
}

bool MountEvilMinfs() {
    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    const char* parent_path = "/tmp/parent";
    ASSERT_EQ(mkdir(parent_path, 0666), 0);
    int mountfd = open(parent_path, O_RDONLY | O_DIRECTORY | O_ADMIN);
    ASSERT_GT(mountfd, 0, "Couldn't open mount point");
    int ramdiskfd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdiskfd, 0);
    ASSERT_EQ(
        fmount(ramdiskfd, mountfd, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
        ZX_OK);
    ASSERT_EQ(close(mountfd), 0);

    const char* mount_path = "/tmp/parent/mount_evil";
    ASSERT_TRUE(DoMountEvil("minfs", mount_path));

    ASSERT_EQ(umount(parent_path), 0);
    ASSERT_EQ(rmdir(parent_path), 0);
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    END_TEST;
}

bool UmountTestEvil() {
    const char* mount_path = "/tmp/umount_test_evil";

    BEGIN_TEST;

    // Create a ramdisk, mount minfs
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    fbl::unique_fd weak_root_fd(open(mount_path, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(weak_root_fd);
    zx_status_t status;
    fzl::FdioCaller caller(std::move(weak_root_fd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminUnmount(caller.borrow_channel(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);
    weak_root_fd.reset(caller.release().release());

    // Try opening a non-root directory without O_ADMIN. We shouldn't be able
    // to umount.
    ASSERT_EQ(mkdirat(weak_root_fd.get(), "subdir", 0666), 0);
    fbl::unique_fd weak_subdir_fd(openat(weak_root_fd.get(), "subdir", O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(weak_subdir_fd);
    caller.reset(std::move(weak_subdir_fd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminUnmount(caller.borrow_channel(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);

    // Try opening a new directory with O_ADMIN. It shouldn't open.
    weak_subdir_fd.reset(openat(weak_root_fd.get(), "subdir", O_RDONLY | O_DIRECTORY | O_ADMIN));
    ASSERT_FALSE(weak_subdir_fd);

    // Finally, umount using O_NOREMOTE and acquiring the connection
    // that has "O_ADMIN" set.
    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool DoubleMountRoot() {
    const char* mount_path = "/tmp/double_mount_root";

    BEGIN_TEST;

    // Create a ramdisk, mount minfs
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    ramdisk_client_t* ramdisk2;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk2), ZX_OK);
    const char* ramdisk_path2 = ramdisk_get_path(ramdisk2);
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
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(ramdisk_destroy(ramdisk2), 0);
    ASSERT_EQ(rmdir(mount_path), 0);
    END_TEST;
}

bool MountRemount() {
    const char* mount_path = "/tmp/mount_remount";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);

    // We should still be able to mount and unmount the filesystem multiple times
    for (size_t i = 0; i < 10; i++) {
        int fd = open(ramdisk_path, O_RDWR);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(
            mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
            ZX_OK);
        ASSERT_EQ(umount(mount_path), ZX_OK);
    }
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool MountFsck() {
    const char* mount_path = "/tmp/mount_fsck";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool MountGetDevice() {
    const char* mount_path = "/tmp/mount_get_device";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));

    fbl::unique_fd mountfd(open(mount_path, O_RDONLY | O_ADMIN));
    ASSERT_TRUE(mountfd);
    char device_buffer[1024];
    char* device_path = static_cast<char*>(device_buffer);
    zx_status_t status;
    size_t path_len;
    fzl::FdioCaller caller(std::move(mountfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminGetDevicePath(caller.borrow_channel(), &status,
                                                     device_path, sizeof(device_buffer),
                                                     &path_len),
              ZX_OK);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "minfs", strlen("minfs")));

    mountfd.reset(open(mount_path, O_RDONLY | O_ADMIN));
    ASSERT_TRUE(mountfd);
    caller.reset(std::move(mountfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminGetDevicePath(caller.borrow_channel(), &status,
                                                     device_path, sizeof(device_buffer),
                                                     &path_len),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_GT(path_len, 0, "Device path not found");
    ASSERT_EQ(strncmp(ramdisk_path, device_path, path_len), 0, "Unexpected device path");

    mountfd.reset(open(mount_path, O_RDONLY));
    ASSERT_TRUE(mountfd);
    caller.reset(std::move(mountfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminGetDevicePath(caller.borrow_channel(), &status,
                                                     device_path, sizeof(device_buffer),
                                                     &path_len),
              ZX_OK);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);

    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_TRUE(CheckMountedFs(mount_path, "memfs", strlen("memfs")));

    mountfd.reset(open(mount_path, O_RDONLY | O_ADMIN));
    ASSERT_TRUE(mountfd);
    caller.reset(std::move(mountfd));
    ASSERT_EQ(fuchsia_io_DirectoryAdminGetDevicePath(caller.borrow_channel(), &status,
                                                     device_path, sizeof(device_buffer),
                                                     &path_len),
              ZX_OK);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);

    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
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
bool MountReadonly() {
    const char* mount_path = "/tmp/mount_readonly";
    const char file_name[] = "some_file";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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

    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);

    END_TEST;
}

// Test that when a block device claims to be read-only, the filesystem is mounted as read-only.
bool MountBlockReadonly() {
    const char* mount_path = "/tmp/mount_readonly";
    const char file_name[] = "some_file";

    BEGIN_TEST;

    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_TRUE(CreateTestFile(ramdisk_path, mount_path, file_name));

    uint32_t flags = BLOCK_FLAG_READONLY;
    ASSERT_EQ(ramdisk_set_flags(ramdisk, flags), ZX_OK);

    bool read_only = false;
    ASSERT_TRUE(MountMinfs(ramdisk_get_block_fd(ramdisk), read_only, mount_path));

    // We can't modify the file.
    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    int fd = openat(root_fd, file_name, O_CREAT | O_RDWR);
    ASSERT_LT(fd, 0);

    // We can open it as read-only.
    fd = openat(root_fd, file_name, O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(root_fd), 0);
    ASSERT_EQ(umount(mount_path), ZX_OK);

    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);

    END_TEST;
}

bool StatfsTest() {
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
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
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

bool StatvfsTest() {
    const char* mount_path = "/tmp/mount_unmount";

    BEGIN_TEST;
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk);
    ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options, launch_stdio_async),
              ZX_OK);

    struct statvfs stats;
    int rc = statvfs("", &stats);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    rc = statvfs(mount_path, &stats);
    ASSERT_EQ(rc, 0);

    // Verify that at least some values make sense, without making the test too brittle.
    ASSERT_NE(stats.f_fsid, 0);
    ASSERT_EQ(stats.f_bsize, 8192u);
    ASSERT_EQ(stats.f_frsize, 8192u);
    ASSERT_EQ(stats.f_namemax, 255u);
    ASSERT_GT(stats.f_bavail, 0u);
    ASSERT_GT(stats.f_ffree, 0u);
    ASSERT_GT(stats.f_favail, 0u);

    ASSERT_EQ(umount(mount_path), ZX_OK);
    ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
    ASSERT_EQ(unlink(mount_path), 0);
    END_TEST;
}

// Verifies that the values in stats match the other parameters
bool CheckStats(fuchsia_hardware_block_BlockStats stats, int64_t total_ops, int64_t total_blocks,
                int64_t total_reads, int64_t total_blocks_read, int64_t total_writes,
                int64_t total_blocks_written) {
    BEGIN_HELPER;
    ASSERT_EQ(stats.ops, total_ops);
    ASSERT_EQ(stats.blocks, total_blocks);
    ASSERT_EQ(stats.reads, total_reads);
    ASSERT_EQ(stats.blocks_read, total_blocks_read);
    ASSERT_EQ(stats.writes, total_writes);
    ASSERT_EQ(stats.blocks_written, total_blocks_written);
    END_HELPER;
}

bool GetPartitionSliceCount(const zx::unowned_channel& channel, size_t* out_count) {
    BEGIN_HELPER;

    fuchsia_hardware_block_volume_VolumeInfo fvm_info;
    zx_status_t status;
    ASSERT_OK(fuchsia_hardware_block_volume_VolumeQuery(channel->get(), &status, &fvm_info));
    ASSERT_OK(status);

    size_t allocated_slices = 0;
    uint64_t start_slices[1];
    start_slices[0] = 0;
    while (start_slices[0] < fvm_info.vslice_count) {
        fuchsia_hardware_block_volume_VsliceRange
            ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
        size_t actual_ranges_count;
        ASSERT_OK(fuchsia_hardware_block_volume_VolumeQuerySlices(
                    channel->get(), start_slices, fbl::count_of(start_slices), &status,
                    ranges, &actual_ranges_count));
        ASSERT_OK(status);
        ASSERT_EQ(1, actual_ranges_count);
        start_slices[0] += ranges[0].count;
        if (ranges[0].allocated) {
            allocated_slices += ranges[0].count;
        }
    }

    *out_count = allocated_slices;
    END_HELPER;
}

// Reformat the partition using a number of slices and verify that there are as many slices as
// originally pre-allocated.
bool MkfsMinfsWithMinFvmSlices(fs_test_utils::Fixture* fixture) {
    BEGIN_TEST;
    mkfs_options_t options = default_mkfs_options;
    size_t base_slices = 0;
    ASSERT_OK(
        mkfs(fixture->partition_path().c_str(), DISK_FORMAT_MINFS, launch_stdio_sync,
             &default_mkfs_options));
    fbl::unique_fd partition_fd(open(fixture->partition_path().c_str(), O_RDONLY));
    ASSERT_TRUE(partition_fd);
    fzl::UnownedFdioCaller caller(partition_fd.get());
    ASSERT_TRUE(GetPartitionSliceCount(zx::unowned_channel(caller.borrow_channel()), &base_slices));
    options.fvm_data_slices += 10;

    ASSERT_OK(
        mkfs(fixture->partition_path().c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, &options));
    size_t allocated_slices = 0;
    ASSERT_TRUE(GetPartitionSliceCount(zx::unowned_channel(caller.borrow_channel()),
                                       &allocated_slices));
    EXPECT_GE(allocated_slices, base_slices + 10);

    disk_format_t actual_format = detect_disk_format(partition_fd.get());
    ASSERT_EQ(actual_format, DISK_FORMAT_MINFS);
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(fs_management_tests)
RUN_TEST_MEDIUM(MountUnmount)
RUN_TEST_MEDIUM(MountUnmountLargeBlock)
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
RUN_TEST_MEDIUM(StatvfsTest)
END_TEST_CASE(fs_management_tests)

BEGIN_FS_TEST_CASE(fs_management_mkfs_tests, PartitionOverFvmWithRamdisk)
RUN_FS_TEST_F(MkfsMinfsWithMinFvmSlices)
END_FS_TEST_CASE(fs_management_mkfs_tests, PartitionOverFvmWithRamdisk)

int main(int argc, char** argv) {
    return fs_test_utils::RunWithMemFs(
        [argc, argv]() { return unittest_run_all_tests(argc, argv) ? 0 : -1; });
}
