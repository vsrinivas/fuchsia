// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fdio/watcher.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/util.h>

#include "block-watcher.h"
#include "devmgr.h"
#include "memfs-private.h"

static zx_handle_t job;
static bool netboot;

void launch_blob_init() {
    const char* blob_init = getenv("zircon.system.blob-init");
    if (blob_init == NULL) {
        return;
    }
    if (secondary_bootfs_ready()) {
        printf("fshost: zircon.system.blob-init ignored due to secondary bootfs\n");
        return;
    }

    zx_handle_t proc = ZX_HANDLE_INVALID;

    uint32_t type = PA_HND(PA_USER0, 0);
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_handle_t pkgfs_root = ZX_HANDLE_INVALID;
    if (zx_channel_create(0, &handle, &pkgfs_root) != ZX_OK) {
        return;
    }

    //TODO: make blob-init a /fs/blob relative path
    const char *argv[2];
    char binary[strlen(blob_init) + 4];
    sprintf(binary, "/fs%s", blob_init);
    argv[0] = binary;
    const char* blob_init_arg = getenv("zircon.system.blob-init-arg");
    int argc = 1;
    if (blob_init_arg != NULL) {
        argc++;
        argv[1] = blob_init_arg;
    }

    zx_status_t status = devmgr_launch(job, "pkgfs", argc, &argv[0], NULL, -1,
                                       &handle, &type, 1, &proc, FS_DATA | FS_BLOB | FS_SVC);

    if (status != ZX_OK) {
        printf("fshost: '%s' failed to launch: %d\n", blob_init, status);
        goto fail0;
    }

    zx_time_t deadline = zx_deadline_after(ZX_SEC(5));
    zx_signals_t observed;
    status = zx_object_wait_one(proc, ZX_USER_SIGNAL_0 | ZX_PROCESS_TERMINATED,
                                deadline, &observed);
    if (status != ZX_OK) {
        printf("fshost: '%s' did not signal completion: %d\n", blob_init, status);
        goto fail0;
    }
    if (!(observed & ZX_USER_SIGNAL_0)) {
        printf("fshost: '%s' terminated prematurely\n", blob_init);
        goto fail0;
    }
    if (vfs_install_fs("/pkgfs", pkgfs_root) != ZX_OK) {
        printf("fshost: failed to install /pkgfs\n");
        goto fail1;
    }

    // re-export /pkgfs/system as /system
    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) != ZX_OK) {
        goto fail1;
    }
    if (fdio_open_at(pkgfs_root, "system", FS_DIR_FLAGS, h1) != ZX_OK) {
        zx_handle_close(h0);
        goto fail1;
    }
    if (vfs_install_fs("/system", h0) != ZX_OK) {
        printf("fshost: failed to install /system\n");
        goto fail1;
    }

    // start the appmgr
    fuchsia_start();
    zx_handle_close(proc);
    return;

fail0:
    zx_handle_close(pkgfs_root);
fail1:
    zx_handle_close(proc);
}

static zx_status_t launch_blobfs(int argc, const char** argv, zx_handle_t* hnd,
                                 uint32_t* ids, size_t len) {
    return devmgr_launch(job, "blobfs:/blob", argc, argv, NULL, -1,
                         hnd, ids, len, NULL, FS_FOR_FSPROC);
}

static zx_status_t launch_minfs(int argc, const char** argv, zx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    return devmgr_launch(job, "minfs:/data", argc, argv, NULL, -1,
                         hnd, ids, len, NULL, FS_FOR_FSPROC);
}

static zx_status_t launch_fat(int argc, const char** argv, zx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    return devmgr_launch(job, "fatfs:/volume", argc, argv, NULL, -1,
                         hnd, ids, len, NULL, FS_FOR_FSPROC);
}

static bool data_mounted = false;
static bool install_mounted = false;
static bool blob_mounted = false;

/*
 * Attempt to mount the device pointed to be the file descriptor at a known
 * location.
 * Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
 * is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
 * GUID of the device does not match a known valid one. Returns ZX_OK if an
 * attempt to mount is made, without checking mount success.
 */
static zx_status_t mount_minfs(int fd, mount_options_t* options) {
    uint8_t type_guid[GPT_GUID_LEN];

    // initialize our data for this run
    ssize_t read_sz = ioctl_block_get_type_guid(fd, type_guid,
                                                sizeof(type_guid));

    // check if this partition matches any special type GUID
    if (read_sz == GPT_GUID_LEN) {
        if (gpt_is_sys_guid(type_guid, read_sz)) {
            if (secondary_bootfs_ready()) {
                return ZX_ERR_ALREADY_BOUND;
            }
            if (getenv("zircon.system.blob-init") != NULL) {
                printf("fshost: minfs system partition ignored due to zircon.system.blob-init\n");
                return ZX_ERR_ALREADY_BOUND;
            }
            const char* volume = getenv("zircon.system.volume");
            if (volume != NULL && !strcmp(volume, "any")) {
                // Fall-through; we'll take anything.
            } else if (volume != NULL && !strcmp(volume, "local")) {
                // Fall-through only if we can guarantee the partition
                // is not removable.
                block_info_t info;
                if ((ioctl_block_get_info(fd, &info) < 0) ||
                    (info.flags & BLOCK_FLAG_REMOVABLE)) {
                    return ZX_ERR_BAD_STATE;
                }
            } else {
                return ZX_ERR_BAD_STATE;
            }

            // TODO(ZX-1008): replace getenv with cmdline_bool("zircon.system.writable", false);
            options->readonly = getenv("zircon.system.writable") == NULL;
            options->wait_until_ready = true;

            zx_status_t st = mount(fd, "/fs" PATH_SYSTEM, DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != ZX_OK) {
                printf("devmgr: failed to mount %s, retcode = %d. Run fixfs to restore partition.\n", PATH_SYSTEM, st);
            } else {
                fuchsia_start();
            }

            return st;
        } else if (gpt_is_data_guid(type_guid, read_sz)) {
            if (data_mounted) {
                return ZX_ERR_ALREADY_BOUND;
            }
            data_mounted = true;
            options->wait_until_ready = true;

            zx_status_t st = mount(fd, "/fs" PATH_DATA, DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != ZX_OK) {
                printf("devmgr: failed to mount %s, retcode = %d. Run fixfs to restore partition.\n", PATH_DATA, st);
            }

            return st;
        } else if (gpt_is_install_guid(type_guid, read_sz)) {
            if (install_mounted) {
                return ZX_ERR_ALREADY_BOUND;
            }
            install_mounted = true;
            options->readonly = true;
            options->wait_until_ready = true;

            zx_status_t st = mount(fd, "/fs" PATH_INSTALL, DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != ZX_OK) {
                printf("devmgr: failed to mount %s, retcode = %d. Run fixfs to restore partition.\n", PATH_INSTALL, st);
            }

            return st;
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

static zx_status_t block_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }

    char device_path[PATH_MAX];
    sprintf(device_path, "%s/%s", PATH_DEV_BLOCK, name);

    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return ZX_OK;
    }

    disk_format_t df = detect_disk_format(fd);

    switch (df) {
    case DISK_FORMAT_GPT: {
        printf("devmgr: %s: GPT?\n", device_path);
        // probe for partition table
        ioctl_device_bind(fd, GPT_DRIVER_LIB, STRLEN(GPT_DRIVER_LIB));
        close(fd);
        return ZX_OK;
    }
    case DISK_FORMAT_FVM: {
        printf("devmgr: /dev/class/block/%s: FVM?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
        close(fd);
        return ZX_OK;
    }
    case DISK_FORMAT_MBR: {
        printf("devmgr: %s: MBR?\n", device_path);
        // probe for partition table
        ioctl_device_bind(fd, MBR_DRIVER_LIB, STRLEN(MBR_DRIVER_LIB));
        close(fd);
        return ZX_OK;
    }
    default:
        break;
    }

    uint8_t guid[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
    ioctl_block_get_type_guid(fd, guid, sizeof(guid));

    // If we're in netbooting mode, then only bind drivers for partition
    // containers and the install partition, not regular filesystems.
    if (netboot) {
        const uint8_t expected_guid[GPT_GUID_LEN] = GUID_INSTALL_VALUE;
        if (memcmp(guid, expected_guid, sizeof(guid)) == 0) {
            printf("devmgr: mounting install partition\n");
            mount_options_t options = default_mount_options;
            options.wait_until_ready = false;
            mount_minfs(fd, &options);
            return ZX_OK;
        }

        close(fd);
        return ZX_OK;
    }

    switch (df) {
    case DISK_FORMAT_BLOBFS: {
        const uint8_t expected_guid[GPT_GUID_LEN] = GUID_BLOB_VALUE;

        if (memcmp(guid, expected_guid, sizeof(guid))) {
            close(fd);
            return ZX_OK;
        }
        if (!blob_mounted) {
            mount_options_t options = default_mount_options;
            zx_status_t status = mount(fd, "/fs" PATH_BLOB, DISK_FORMAT_BLOBFS,
                                       &options, launch_blobfs);
            if (status != ZX_OK) {
                printf("devmgr: Failed to mount blobfs partition %s at %s: %d. Please run fixfs to reformat.\n", device_path, PATH_BLOB, status);
            } else {
                blob_mounted = true;
                launch_blob_init();
            }
        }

        return ZX_OK;
    }
    case DISK_FORMAT_MINFS: {
        printf("devmgr: mounting minfs\n");
        mount_options_t options = default_mount_options;
        options.wait_until_ready = false;
        mount_minfs(fd, &options);
        return ZX_OK;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition
        uint8_t guid[GPT_GUID_LEN];
        ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
        bool efi = gpt_is_efi_guid(guid, r);
        if (efi) {
            close(fd);
            printf("devmgr: not automounting efi\n");
            return ZX_OK;
        }
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        static int fat_counter = 0;
        char mountpath[FDIO_MAX_FILENAME + 64];
        snprintf(mountpath, sizeof(mountpath), "%s/fat-%d", "/fs" PATH_VOLUME, fat_counter++);
        options.wait_until_ready = false;
        printf("devmgr: mounting fatfs\n");
        mount(fd, mountpath, df, &options, launch_fat);
        return ZX_OK;
    }
    default:
        close(fd);
        return ZX_OK;
    }
}

void block_device_watcher(zx_handle_t _job, bool _netboot) {
    job = _job;
    netboot = _netboot;

    int dirfd;
    if ((dirfd = open("/dev/class/block", O_DIRECTORY | O_RDONLY)) >= 0) {
        fdio_watch_directory(dirfd, block_device_added, ZX_TIME_INFINITE, &job);
    }
    close(dirfd);
}
