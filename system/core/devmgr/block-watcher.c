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
                         hnd, ids, len);
}

static mx_status_t launch_minfs(int argc, const char** argv, mx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    return devmgr_launch(job, "minfs:/data", argc, argv, NULL, -1,
                         hnd, ids, len);
}

static mx_status_t launch_fat(int argc, const char** argv, mx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    return devmgr_launch(job, "fatfs:/volume", argc, argv, NULL, -1,
                         hnd, ids, len);
}

static bool data_mounted = false;

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
    static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;

    // initialize our data for this run
    ssize_t read_sz = ioctl_block_get_type_guid(fd, type_guid,
                                                sizeof(type_guid));

    // check if this partition matches any special type GUID
    if (read_sz == GPT_GUID_LEN) {
        if (!memcmp(type_guid, sys_guid, GPT_GUID_LEN)) {
            if (secondary_bootfs_ready()) {
                return MX_ERR_ALREADY_BOUND;
            }

            options->readonly = true;
            options->wait_until_ready = true;
            options->create_mountpoint = true;

            mx_status_t st = mount(fd, "/system", DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount /system, retcode = %d\n", st);
            } else {
                devmgr_start_appmgr(NULL);
            }

            return MX_OK;
        } else if (!memcmp(type_guid, data_guid, GPT_GUID_LEN)) {
            if (data_mounted) {
                return MX_ERR_ALREADY_BOUND;
            }
            data_mounted = true;
            options->wait_until_ready = true;

            mx_status_t st = mount(fd, "/data", DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount /data, retcode = %d\n", st);
            }

            return MX_OK;
        }
    }

    return MX_ERR_INVALID_ARGS;
}

#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define STRLEN(s) sizeof(s)/sizeof((s)[0])

static mx_status_t block_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        printf("devmgr: block watch waiting...\n");
        return MX_OK;
    }

    printf("devmgr: new block device: /dev/class/block/%s\n", name);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return MX_OK;
    }

    disk_format_t df = detect_disk_format(fd);

    switch (df) {
    case DISK_FORMAT_GPT: {
        printf("devmgr: /dev/class/block/%s: GPT?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, GPT_DRIVER_LIB, STRLEN(GPT_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_MBR: {
        printf("devmgr: /dev/class/block/%s: MBR?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, MBR_DRIVER_LIB, STRLEN(MBR_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_BLOBFS: {
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        mount(fd, "/blobstore", DISK_FORMAT_BLOBFS, &options, launch_blobstore);
        return MX_OK;
    }
    case DISK_FORMAT_MINFS: {
        mount_options_t options = default_mount_options;
        options.wait_until_ready = false;
        printf("devmgr: minfs\n");
        if (mount_minfs(fd, &options) != MX_OK) {
            close(fd);
        }
        return MX_OK;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition as writable
        uint8_t guid[GPT_GUID_LEN];
        ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
        bool efi = false;
        static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;
        if (r == GPT_GUID_LEN && !memcmp(guid, guid_efi_part, GPT_GUID_LEN)) {
            efi = true;
        }
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        options.readonly = efi;
        static int fat_counter = 0;
        static int efi_counter = 0;
        char mountpath[MXIO_MAX_FILENAME + 64];
        if (efi) {
            snprintf(mountpath, sizeof(mountpath), "/volume/efi-%d", efi_counter++);
        } else {
            snprintf(mountpath, sizeof(mountpath), "/volume/fat-%d", fat_counter++);
        }
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
    if ((dirfd = open("/dev/class/block", O_DIRECTORY|O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, block_device_added, MX_TIME_INFINITE, &job);
    }
    close(dirfd);
}