// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

#define HEADER_SIZE 4096
#define arraylen(arr) (sizeof(arr) / sizeof(arr[0]))

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00,
    0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

static const uint8_t gpt_magic[16] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54,
    0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
};

disk_format_t detect_disk_format(int fd) {
    uint8_t data[HEADER_SIZE];
    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Error reading block device\n");
        return -1;
    }

    if (!memcmp(data + 0x200, gpt_magic, sizeof(gpt_magic))) {
        return DISK_FORMAT_GPT;
    } else if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        return DISK_FORMAT_MINFS;
    } else if ((data[510] == 0x55 && data[511] == 0xAA) &&
               (data[38] == 0x29 || data[66] == 0x29)) {
        // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
        // 0x29 is the Boot Signature, but it is placed at either offset 38 or
        // 66 (depending on FAT type).
        return DISK_FORMAT_FAT;
    }
    return DISK_FORMAT_UNKNOWN;
}

static mx_status_t mount_remote_handle(const char* where, mx_handle_t* h) {
    int fd;
    if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
        return ERR_BAD_STATE;
    }
    mx_status_t status = NO_ERROR;
    if (ioctl_devmgr_mount_fs(fd, h) != sizeof(mx_handle_t)) {
        fprintf(stderr, "fs_mount: Could not mount remote handle on %s\n", where);
        status = ERR_BAD_STATE;
    }
    close(fd);
    return status;
}

static mx_status_t mount_minfs(int devicefd, const char* mountpath, const mount_options_t* options,
                               LaunchCallback cb) {
    mx_handle_t hnd[MXIO_MAX_HANDLES * 2];
    uint32_t ids[MXIO_MAX_HANDLES * 2];
    size_t n = 0;
    mx_status_t status;
    if ((status = mount_remote_handle(mountpath, hnd)) != NO_ERROR) {
        fprintf(stderr, "Failed to mount remote handle handle on mount path\n");
        close(devicefd);
        return status;
    }
    ids[n] = MX_HND_TYPE_USER0;
    n++;
    if ((status = mxio_transfer_fd(devicefd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        close(devicefd);
        return status != 0 ? status : ERR_BAD_STATE;
    }
    n += status;

    if (options->verbose_mount) {
        printf("fs_mount: Launching Minfs\n");
    }
    const char* argv[] = { "/boot/bin/minfs", "mount" };
    return cb(arraylen(argv), argv, hnd, ids, n);
}

static mx_status_t mount_fat(int devicefd, const char* mountpath, const mount_options_t* options,
                             LaunchCallback cb) {
    mx_handle_t hnd[MXIO_MAX_HANDLES * 2];
    uint32_t ids[MXIO_MAX_HANDLES * 2];
    size_t n = 0;
    mx_status_t status;
    if ((status = mount_remote_handle(mountpath, hnd)) != NO_ERROR) {
        fprintf(stderr, "Failed to mount remote handle handle on mount path\n");
        close(devicefd);
        return status;
    }
    ids[n] = MX_HND_TYPE_USER0;
    n++;
    if ((status = mxio_transfer_fd(devicefd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        close(devicefd);
        return status != 0 ? status : ERR_BAD_STATE;
    }
    n += status;

    char readonly_arg[64];
    snprintf(readonly_arg, sizeof(readonly_arg), "-readonly=%s",
             options->readonly ? "true" : "false");
    char blockfd_arg[64];
    snprintf(blockfd_arg, sizeof(blockfd_arg), "-blockFD=%d", FS_FD_BLOCKDEVICE);

    if (options->verbose_mount) {
        printf("fs_mount: Launching ThinFS\n");
    }
    const char* argv[] = {
        "/system/bin/thinfs",
        readonly_arg,
        blockfd_arg,
        "mount",
    };
    return cb(arraylen(argv), argv, hnd, ids, n);
}

mx_status_t mount(int devicefd, const char* mountpath,
                  disk_format_t df, const mount_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return mount_minfs(devicefd, mountpath, options, cb);
    case DISK_FORMAT_FAT:
        return mount_fat(devicefd, mountpath, options, cb);
    default:
        close(devicefd);
        return ERR_NOT_SUPPORTED;
    }
}

mx_status_t umount(const char* mountpath) {
    int fd = open(mountpath, O_DIRECTORY | O_NOREMOTE);
    if (fd < 0) {
        fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
        return ERR_BAD_STATE;
    }

    mx_status_t status = ioctl_devmgr_unmount_node(fd);
    if (status < 0) {
        fprintf(stderr, "Could not unmount filesystem: %d\n", status);
    }
    close(fd);
    return status;
}
