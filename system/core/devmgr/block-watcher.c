// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <loader-service/loader-service.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "block-watcher.h"
#include "devmgr.h"
#include "memfs-private.h"

static zx_handle_t job;
static bool netboot;

static zx_status_t fshost_launch_load(void* ctx, launchpad_t* lp,
                                      const char* file) {
    return launchpad_load_from_file(lp, file);
}

static void pkgfs_finish(zx_handle_t proc, zx_handle_t pkgfs_root) {
    zx_time_t deadline = zx_deadline_after(ZX_SEC(5));
    zx_signals_t observed;
    zx_status_t status = zx_object_wait_one(
        proc, ZX_USER_SIGNAL_0 | ZX_PROCESS_TERMINATED, deadline, &observed);
    if (status != ZX_OK) {
        printf("fshost: pkgfs did not signal completion: %d (%s)\n",
               status, zx_status_get_string(status));
        goto fail0;
    }
    if (!(observed & ZX_USER_SIGNAL_0)) {
        printf("fshost: pkgfs terminated prematurely\n");
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

// TODO(mcgrathr): Remove this fallback path when the old args
// are no longer used.
static void old_launch_blob_init(void) {
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

    zx_status_t status = devmgr_launch(
        job, "pkgfs", &fshost_launch_load, NULL, argc, &argv[0], NULL, -1,
        &handle, &type, 1, &proc, FS_DATA | FS_BLOB | FS_SVC);

    if (status != ZX_OK) {
        printf("fshost: '%s' failed to launch: %d\n", blob_init, status);
        zx_handle_close(handle);
        zx_handle_close(pkgfs_root);
        return;
    }

    pkgfs_finish(proc, pkgfs_root);
}

// Launching pkgfs uses its own loader service and command lookup to run out of
// the blobfs without any real filesystem.  Files are found by
// getenv("zircon.system.pkgfs.file.PATH") returning a blob content ID.
// That is, a manifest of name->blob is embedded in /boot/config/devmgr.
static zx_status_t pkgfs_ldsvc_load_blob(void* ctx, const char* prefix,
                                         const char* name, zx_handle_t* vmo) {
    const int fs_blob_fd = (intptr_t)ctx;
    char key[256];
    if (snprintf(key, sizeof(key), "zircon.system.pkgfs.file.%s%s",
                 prefix, name) >= (int)sizeof(key)) {
        return ZX_ERR_BAD_PATH;
    }
    const char *blob = getenv(key);
    if (blob == NULL) {
        return ZX_ERR_NOT_FOUND;
    }
    int fd = openat(fs_blob_fd, blob, O_RDONLY);
    if (fd < 0) {
        return ZX_ERR_NOT_FOUND;
    }
    zx_status_t status = fdio_get_vmo_clone(fd, vmo);
    close(fd);
    if (status == ZX_OK) {
        zx_object_set_property(*vmo, ZX_PROP_NAME, key, strlen(key));
    }
    return status;
}

static zx_status_t pkgfs_ldsvc_load_object(void* ctx, const char* name,
                                           zx_handle_t* vmo) {
    return pkgfs_ldsvc_load_blob(ctx, "lib/", name, vmo);
}

static zx_status_t pkgfs_ldsvc_load_abspath(void* ctx, const char* name,
                                            zx_handle_t* vmo) {
    return pkgfs_ldsvc_load_blob(ctx, "", name + 1, vmo);
}

static zx_status_t pkgfs_ldsvc_publish_data_sink(void* ctx, const char* name,
                                                 zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

static void pkgfs_ldsvc_finalizer(void* ctx) {
    close((intptr_t)ctx);
}

static const loader_service_ops_t pkgfs_ldsvc_ops = {
    .load_object = pkgfs_ldsvc_load_object,
    .load_abspath = pkgfs_ldsvc_load_abspath,
    .publish_data_sink = pkgfs_ldsvc_publish_data_sink,
    .finalizer = pkgfs_ldsvc_finalizer,
};

// Create a local loader service with a fixed mapping of names to blobs.
// Always consumes fs_blob_fd.
static zx_status_t pkgfs_ldsvc_start(int fs_blob_fd, zx_handle_t* ldsvc) {
    loader_service_t* service;
    zx_status_t status = loader_service_create(NULL, &pkgfs_ldsvc_ops,
                                               (void*)(intptr_t)fs_blob_fd,
                                               &service);
    if (status != ZX_OK) {
        printf("fshost: cannot create pkgfs loader service: %d (%s)\n",
               status, zx_status_get_string(status));
        close(fs_blob_fd);
        return status;
    }
    status = loader_service_connect(service, ldsvc);
    loader_service_release(service);
    if (status != ZX_OK) {
        printf("fshost: cannot connect pkgfs loader service: %d (%s)\n",
               status, zx_status_get_string(status));
    }
    return status;
}

// This is the callback to load the file via launchpad.  First look up the
// file itself.  Then get the loader service started so it can service
// launchpad's request for the PT_INTERP file.  Then load it up.
static zx_status_t pkgfs_launch_load(void* ctx, launchpad_t* lp,
                                     const char* file) {
    while (file[0] == '/') {
        ++file;
    }
    zx_handle_t vmo;
    zx_status_t status = pkgfs_ldsvc_load_blob(ctx, "", file, &vmo);
    const int fs_blob_fd = (intptr_t)ctx;
    if (status == ZX_OK) {
        // The service takes ownership of fs_blob_fd.
        zx_handle_t ldsvc;
        status = pkgfs_ldsvc_start(fs_blob_fd, &ldsvc);
        if (status == ZX_OK) {
            launchpad_use_loader_service(lp, ldsvc);
            launchpad_load_from_vmo(lp, vmo);
        } else {
            zx_handle_close(vmo);
        }
    } else {
        close(fs_blob_fd);
    }
    return status;
}

static bool pkgfs_launch(void) {
    const char* cmd = getenv("zircon.system.pkgfs.cmd");
    if (cmd == NULL) {
        return false;
    }

    int fs_blob_fd = open("/fs/blob", O_RDONLY | O_DIRECTORY);
    if (fs_blob_fd < 0) {
        printf("fshost: open(/fs/blob): %m\n");
        return false;
    }

    zx_handle_t h0, h1;
    zx_status_t status = zx_channel_create(0, &h0, &h1);
    if (status != ZX_OK) {
        printf("fshost: cannot create pkgfs root channel: %d (%s)\n",
               status, zx_status_get_string(status));
        close(fs_blob_fd);
        return false;
    }

    zx_handle_t proc = ZX_HANDLE_INVALID;
    status = devmgr_launch_cmdline(
        "fshost", job, "pkgfs",
        &pkgfs_launch_load, (void*)(intptr_t)fs_blob_fd, cmd,
        &h1, (const uint32_t[]){ PA_HND(PA_USER0, 0) }, 1,
        &proc, FS_DATA | FS_BLOB | FS_SVC);
    if (status != ZX_OK) {
        printf("fshost: failed to launch %s: %d (%s)\n",
               cmd, status, zx_status_get_string(status));
        return false;
    }

    pkgfs_finish(proc, h0);
    return true;
}

static void launch_blob_init(void) {
    if (!pkgfs_launch()) {
        // TODO(mcgrathr): Remove when the old args are no longer used.
        old_launch_blob_init();
    }
}

static zx_status_t launch_blobfs(int argc, const char** argv, zx_handle_t* hnd,
                                 uint32_t* ids, size_t len) {
    return devmgr_launch(job, "blobfs:/blob",
                         &fshost_launch_load, NULL, argc, argv, NULL, -1,
                         hnd, ids, len, NULL, FS_FOR_FSPROC);
}

static zx_status_t launch_minfs(int argc, const char** argv, zx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    return devmgr_launch(job, "minfs:/data",
                         &fshost_launch_load, NULL, argc, argv, NULL, -1,
                         hnd, ids, len, NULL, FS_FOR_FSPROC);
}

static zx_status_t launch_fat(int argc, const char** argv, zx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    return devmgr_launch(job, "fatfs:/volume",
                         &fshost_launch_load, NULL, argc, argv, NULL, -1,
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
                printf("devmgr: failed to mount %s: %s.\n", PATH_SYSTEM, zx_status_get_string(st));
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
                printf("devmgr: failed to mount %s: %s.\n", PATH_DATA, zx_status_get_string(st));
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
                printf("devmgr: failed to mount %s: %s.\n", PATH_INSTALL, zx_status_get_string(st));
            }

            return st;
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define BOOTPART_DRIVER_LIB "/boot/driver/bootpart.so"
#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"
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

    block_info_t info;
    if (ioctl_block_get_info(fd, &info) >= 0 && info.flags & BLOCK_FLAG_BOOTPART) {
        ioctl_device_bind(fd, BOOTPART_DRIVER_LIB, STRLEN(BOOTPART_DRIVER_LIB));
        close(fd);
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
    case DISK_FORMAT_ZXCRYPT: {
        printf("devmgr: %s: zxcrypt?\n", device_path);
        // TODO(security): ZX-1130. We need to bind with channel in order to pass a key here.
        // Where does the key come from?  We need to determine if this is unattended.
        ioctl_device_bind(fd, ZXCRYPT_DRIVER_LIB, STRLEN(ZXCRYPT_DRIVER_LIB));
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
                printf("devmgr: Failed to mount blobfs partition %s at %s: %s.\n",
                       device_path, PATH_BLOB, zx_status_get_string(status));
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
