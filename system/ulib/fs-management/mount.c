// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>
#include <fs/vfs-client.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/limits.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#define HEADER_SIZE 4096

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00,
    0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

static const uint8_t blobstore_magic[16] = {
    0x21, 0x4d, 0x69, 0x9e, 0x47, 0x53, 0x21, 0xac,
    0x14, 0xd3, 0xd3, 0xd4, 0xd4, 0x00, 0x50, 0x98,
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
    } else if (!memcmp(data, blobstore_magic, sizeof(blobstore_magic))) {
        return DISK_FORMAT_BLOBFS;
    } else if ((data[510] == 0x55 && data[511] == 0xAA)) {
        if ((data[38] == 0x29 || data[66] == 0x29)) {
            // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
            // 0x29 is the Boot Signature, but it is placed at either offset 38 or
            // 66 (depending on FAT type).
            return DISK_FORMAT_FAT;
        } else {
            return DISK_FORMAT_MBR;
        }
    }
    return DISK_FORMAT_UNKNOWN;
}

// Initializes 'hnd' and 'ids' with the root handle and block device handle.
// Consumes devicefd.
static mx_status_t mount_prepare_handles(int devicefd, mx_handle_t* mount_handle_out,
                                         mx_handle_t* hnd, uint32_t* ids, size_t* n) {
    mx_status_t status;
    mx_handle_t mountee_handle;
    if ((status = mx_channel_create(0, &mountee_handle, mount_handle_out)) != NO_ERROR) {
        close(devicefd);
        return status;
    }
    hnd[*n] = mountee_handle;
    ids[*n] = PA_USER0;
    *n = *n + 1;

    if ((status = mxio_transfer_fd(devicefd, FS_FD_BLOCKDEVICE, hnd + *n, ids + *n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        mx_handle_close(mountee_handle);
        mx_handle_close(*mount_handle_out);
        close(devicefd);
        return status != 0 ? status : ERR_BAD_STATE;
    }
    *n = *n + status;
    return NO_ERROR;
}

// Describes the mountpoint of the to-be-mounted root,
// either by fd or by path (but never both).
typedef struct mountpoint {
    union {
        const char* path;
        int fd;
    };
    uint32_t flags;
} mountpoint_t;

// Calls the 'launch callback' and mounts the remote handle to the target vnode, if successful.
static mx_status_t launch_and_mount(LaunchCallback cb, const mount_options_t* options,
                                    const char** argv, int argc, mx_handle_t* hnd,
                                    uint32_t* ids, size_t n, mountpoint_t* mp, mx_handle_t root) {
    mx_status_t status;
    if ((status = cb(argc, argv, hnd, ids, n)) != NO_ERROR) {
        return status;
    }

    if (options->wait_until_ready) {
        // Wait until the filesystem is ready to take incoming requests
        mx_signals_t observed;
        status = mx_object_wait_one(root, MX_USER_SIGNAL_0 | MX_CHANNEL_PEER_CLOSED,
                                    MX_TIME_INFINITE, &observed);
        if ((status != NO_ERROR) || (observed & MX_CHANNEL_PEER_CLOSED)) {
            status = (status != NO_ERROR) ? status : ERR_BAD_STATE;
            goto fail;
        }
    }

    // Install remote handle.
    if (options->create_mountpoint) {
        int fd = open("/", O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            goto fail;
        }

        size_t config_size = sizeof(mount_mkdir_config_t) + strlen(mp->path) + 1;
        mount_mkdir_config_t* config = malloc(config_size);
        if (config == NULL) {
            close(fd);
            goto fail;
        }
        config->fs_root = root;
        config->flags = mp->flags;
        strcpy(config->name, mp->path);
        status = ioctl_vfs_mount_mkdir_fs(fd, config, config_size);
        // Currently, the recipient of the ioctl is sending the unmount signal
        // if an error occurs.
        close(fd);
        free(config);
        return status;
    } else {
        if ((status = ioctl_vfs_mount_fs(mp->fd, &root)) != NO_ERROR) {
            // TODO(smklein): Retreive the root handle if mounting fails.
            // Currently, the recipient of the ioctl is sending the unmount signal
            // if an error occurs.
            return status;
        }
    }

    return NO_ERROR;

fail:
    // We've entered a failure case where the filesystem process (which may or may not be alive)
    // had a *chance* to be spawned, but cannot be attached to a vnode (for whatever reason).
    // Rather than abandoning the filesystem process (maybe causing dirty bits to be set), give it a
    // chance to shutdown properly.
    //
    // The unmount process is a little atypical, since we're just sending a signal over a handle,
    // rather than detaching the mounted filesytem from the "parent" filesystem.
    vfs_unmount_handle(root, options->wait_until_ready ? MX_TIME_INFINITE : 0);
    return status;
}

static mx_status_t mount_mxfs(const char* binary, int devicefd, mountpoint_t* mp,
                              const mount_options_t* options, LaunchCallback cb) {
    mx_handle_t hnd[MXIO_MAX_HANDLES * 2];
    uint32_t ids[MXIO_MAX_HANDLES * 2];
    size_t n = 0;
    mx_handle_t root;
    mx_status_t status;
    if ((status = mount_prepare_handles(devicefd, &root, hnd, ids, &n)) != NO_ERROR) {
        return status;
    }

    if (options->verbose_mount) {
        printf("fs_mount: Launching %s\n", binary);
    }
    const char* argv[] = { binary, "mount" };
    return launch_and_mount(cb, options, argv, countof(argv), hnd, ids, n, mp, root);
}

static mx_status_t mount_fat(int devicefd, mountpoint_t* mp, const mount_options_t* options,
                             LaunchCallback cb) {
    mx_handle_t hnd[MXIO_MAX_HANDLES * 2];
    uint32_t ids[MXIO_MAX_HANDLES * 2];
    size_t n = 0;
    mx_handle_t root;
    mx_status_t status;
    if ((status = mount_prepare_handles(devicefd, &root, hnd, ids, &n)) != NO_ERROR) {
        return status;
    }

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
    return launch_and_mount(cb, options, argv, countof(argv), hnd, ids, n, mp, root);
}

mx_status_t fmount_common(int devicefd, mountpoint_t* mp, disk_format_t df,
                          const mount_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return mount_mxfs("/boot/bin/minfs", devicefd, mp, options, cb);
    case DISK_FORMAT_BLOBFS:
        return mount_mxfs("/boot/bin/blobstore", devicefd, mp, options, cb);
    case DISK_FORMAT_FAT:
        return mount_fat(devicefd, mp, options, cb);
    default:
        close(devicefd);
        return ERR_NOT_SUPPORTED;
    }
}

mx_status_t fmount(int devicefd, int mountfd, disk_format_t df,
                   const mount_options_t* options, LaunchCallback cb) {
    mountpoint_t mp;
    mp.fd = mountfd;
    mp.flags = 0;

    return fmount_common(devicefd, &mp, df, options, cb);
}

mx_status_t mount(int devicefd, const char* mountpath, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb) {
    mountpoint_t mp;
    mp.flags = 0;

    if (options->create_mountpoint) {
        // Using 'path' for mountpoint
        mp.path = mountpath;
    } else {
        // Open mountpoint; use it directly
        if ((mp.fd = open(mountpath, O_RDONLY | O_DIRECTORY)) < 0) {
            return ERR_BAD_STATE;
        }
    }

    mx_status_t status = fmount_common(devicefd, &mp, df, options, cb);
    if (!options->create_mountpoint) {
        close(mp.fd);
    }
    return status;
}

mx_status_t fumount(int mountfd) {
    mx_handle_t h;
    mx_status_t status = ioctl_vfs_unmount_node(mountfd, &h);
    if (status < 0) {
        fprintf(stderr, "Could not unmount filesystem: %d\n", status);
    } else {
        status = vfs_unmount_handle(h, MX_TIME_INFINITE);
    }
    return status;
}

mx_status_t umount(const char* mountpath) {
    int fd = open(mountpath, O_DIRECTORY | O_NOREMOTE);
    if (fd < 0) {
        fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
        return ERR_BAD_STATE;
    }
    mx_status_t status = fumount(fd);
    close(fd);
    return status;
}
