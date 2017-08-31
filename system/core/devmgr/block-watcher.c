// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <mxio/watcher.h>

#include "devmgr.h"

static mx_handle_t job;

static mx_status_t launch_blobstore(int argc, const char** argv, mx_handle_t* hnd,
                                    uint32_t* ids, size_t len) {
    return devmgr_launch(job, "blobstore:/blobstore", argc, argv, NULL, -1,
                         hnd, ids, len, NULL);
}

static mx_status_t launch_minfs(int argc, const char** argv, mx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    return devmgr_launch(job, "minfs:/data", argc, argv, NULL, -1,
                         hnd, ids, len, NULL);
}

static mx_status_t launch_fat(int argc, const char** argv, mx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    return devmgr_launch(job, "fatfs:/volume", argc, argv, NULL, -1,
                         hnd, ids, len, NULL);
}

static bool data_mounted = false;
static bool blobstore_mounted = false;

/*
 * Attempt to mount the device pointed to be the file descriptor at a known
 * location.
 * Returns MX_ERR_ALREADY_BOUND if the device could be mounted, but something
 * is already mounted at that location. Returns MX_ERR_INVALID_ARGS if the
 * GUID of the device does not match a known valid one. Returns MX_OK if an
 * attempt to mount is made, without checking mount success.
 */
static mx_status_t mount_minfs(int fd, mount_options_t* options) {
    uint8_t type_guid[GPT_GUID_LEN];

    // initialize our data for this run
    ssize_t read_sz = ioctl_block_get_type_guid(fd, type_guid,
                                                sizeof(type_guid));

    // check if this partition matches any special type GUID
    if (read_sz == GPT_GUID_LEN) {
        if (gpt_is_sys_guid(type_guid, read_sz)) {
            if (secondary_bootfs_ready()) {
                return MX_ERR_ALREADY_BOUND;
            }

            // TODO(MG-1008): replace getenv with cmdline_bool("magenta.system.writable", false);
            options->readonly = getenv("magenta.system.writable") == NULL;
            options->wait_until_ready = true;
            options->create_mountpoint = true;

            mx_status_t st = mount(fd, PATH_SYSTEM, DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount %s, retcode = %d. Run fixfs to restore partition.\n", PATH_SYSTEM, st);
            } else {
                devmgr_start_appmgr(NULL);
            }

            return st;
        } else if (gpt_is_data_guid(type_guid, read_sz)) {
            if (data_mounted) {
                return MX_ERR_ALREADY_BOUND;
            }
            data_mounted = true;
            options->wait_until_ready = true;

            mx_status_t st = mount(fd, PATH_DATA, DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount %s, retcode = %d. Run fixfs to restore partition.\n", PATH_DATA, st);
            }

            return st;
        }
    }

    return MX_ERR_INVALID_ARGS;
}

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

static mx_status_t block_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        printf("devmgr: block watch waiting...\n");
        return MX_OK;
    }

    char device_path[PATH_MAX];
    sprintf(device_path, "%s/%s", PATH_DEV_BLOCK, name);

    printf("devmgr: new block device: %s\n", device_path);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return MX_OK;
    }

    disk_format_t df = detect_disk_format(fd);

    switch (df) {
    case DISK_FORMAT_FVM: {
        printf("devmgr: /dev/class/block/%s: FVM?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_GPT: {
        printf("devmgr: %s: GPT?\n", device_path);
        // probe for partition table
        ioctl_device_bind(fd, GPT_DRIVER_LIB, STRLEN(GPT_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_MBR: {
        printf("devmgr: %s: MBR?\n", device_path);
        // probe for partition table
        ioctl_device_bind(fd, MBR_DRIVER_LIB, STRLEN(MBR_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_BLOBFS: {
        if (!blobstore_mounted) {
            mount_options_t options = default_mount_options;
            options.create_mountpoint = true;
            mx_status_t status = mount(fd, PATH_BLOBSTORE, DISK_FORMAT_BLOBFS, &options, launch_blobstore);
            if (status != MX_OK) {
                printf("devmgr: Failed to mount blobstore partition %s at %s: %d. Please run fixfs to reformat.\n", device_path, PATH_BLOBSTORE, status);
            } else {
                blobstore_mounted = true;
            }
        }

        return MX_OK;
    }
    case DISK_FORMAT_MINFS: {
        printf("devmgr: minfs\n");
        mount_options_t options = default_mount_options;
        options.wait_until_ready = false;
        mount_minfs(fd, &options);
        return MX_OK;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition
        uint8_t guid[GPT_GUID_LEN];
        ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
        bool efi = gpt_is_efi_guid(guid, r);
        if (efi) {
          close(fd);
          printf("devmgr: not automounting efi\n");
          return MX_OK;
        }
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        static int fat_counter = 0;
        char mountpath[MXIO_MAX_FILENAME + 64];
        snprintf(mountpath, sizeof(mountpath), "%s/fat-%d", PATH_VOLUME, fat_counter++);
        options.wait_until_ready = false;
        printf("devmgr: fatfs\n");
        mount(fd, mountpath, df, &options, launch_fat);
        return MX_OK;
    }
    default:
        close(fd);
        return MX_OK;
    }
}

void block_device_watcher(mx_handle_t _job) {
    job = _job;

    int dirfd;
    if ((dirfd = open("/dev/class/block", O_DIRECTORY | O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, block_device_added, MX_TIME_INFINITE, &job);
    }
    close(dirfd);
}
