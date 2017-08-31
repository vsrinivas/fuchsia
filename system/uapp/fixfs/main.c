// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdalign.h>
#include <string.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/device/device.h>
#include <magenta/device/vfs.h>

typedef union {
    vfs_query_info_t info;
    struct {
        alignas(vfs_query_info_t) char h[sizeof(vfs_query_info_t)];
        char name[MAX_FS_NAME_LEN + 1];
    };
} vfs_query_info_wrapper_t;

void check_and_remount(const char* device_path, const char* mount_path, disk_format_t disk_format, mount_options_t* mount_options) {
    int mountfd = open(mount_path, O_RDONLY);

    if (mountfd > 0) {
        vfs_query_info_wrapper_t wrapper;
        ssize_t r = ioctl_vfs_query_fs(mountfd, &wrapper.info, sizeof(wrapper) - 1);
        close(mountfd);

        if (r <= (ssize_t)sizeof(vfs_query_info_t)) {
            return;
        }

        wrapper.name[r - sizeof(vfs_query_info_t)] = '\0';
        if (strcmp(wrapper.name, "memfs")) {
            return;
        }
    } else if (mountfd != MX_ERR_INTERNAL) {
        printf("fixfs: couldn't open: %s %d\n", mount_path, mountfd);
        return;
    }

    printf("fixfs: Found device %s not mounted at %s - proceed with reformat? (y/n)\n", device_path, mount_path);
    char response;
    scanf("%c", &response);

    if (response != 'y') {
        return;
    }

    mkfs_options_t mkfs_options = default_mkfs_options;

    ssize_t r;
    if ((r = mkfs(device_path, disk_format, launch_stdio_sync, &mkfs_options)) < 0) {
        fprintf(stderr, "fixfs: Failed to format device %s: %ld\n", device_path, r);
        return;
    }

    int devfd;
    if ((devfd = open(device_path, O_RDWR)) < 0) {
        fprintf(stderr, "fixfs: Error opening block device %s\n", device_path);
        return;
    }

    mx_status_t status = mount(devfd, mount_path, disk_format, mount_options, launch_logs_async);
    if (status != MX_OK) {
        fprintf(stderr, "fixfs: Error while mounting %s at %s: %d\n", device_path, mount_path, status);
    } else {
        printf("fixfs: Successfully mounted device %s at %s\n", device_path, mount_path);
    }

    close(devfd);
}

void check_and_remount_fat(const char* device_path, const char* topo_path, mount_options_t* mount_options) {
    char mount_path[PATH_MAX] = "\0";
    int dirfd = open(PATH_VOLUME, O_RDONLY);
    bool found = false;

    if (dirfd < 0) {
        printf("fixfs: Error opening %s\n", PATH_VOLUME);
        return;
    }

    DIR* dir = fdopendir(dirfd);

    if (!dir) {
        printf("fixfs: Error opening %s\n", PATH_VOLUME);
        return;
    }

    // This loop has two purposes: First, to find if the device is mounted at any of
    // the existing paths under volume/. Second, to find the first "free" mount path
    // under volume/ so that we can mount the partition there (if we don't find it at
    // another path). Note that if we find an unmounted device but no "free" mount path,
    // the device will not be mounted.
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        int mountfd = openat(dirfd, de->d_name, O_RDONLY);
        if (mountfd < 0) {
            printf("fixfs: Error opening %s/%s\n", PATH_VOLUME, de->d_name);
            continue;
        }

        if (!strcmp(de->d_name, ".")) {
            close(mountfd);
            continue;
        }

        char reported_path[PATH_MAX];
        vfs_query_info_wrapper_t wrapper;
        ssize_t r = ioctl_vfs_query_fs(mountfd, &wrapper.info, sizeof(wrapper) - 1);
        ssize_t path_len = ioctl_vfs_get_device_path(mountfd, reported_path, sizeof(reported_path));
        close(mountfd);

        if (r <= (ssize_t)sizeof(vfs_query_info_t)) {
            continue;
        }

        wrapper.name[r - sizeof(vfs_query_info_t)] = '\0';

        // use the first memfs path we find as the mount path
        if (!strcmp(wrapper.name, "memfs")) {
            if (strlen(mount_path) == 0) {
                snprintf(mount_path, sizeof(mount_path), "%s/%s", PATH_VOLUME, de->d_name);
            }
        }

        // return if we found where this device is mounted
        if (path_len > 0 && !strcmp(topo_path, reported_path)) {
            found = true;
            break;
        }
    }

    closedir(dir);
    close(dirfd);

    if (found) {
        return;
    }

    if (strlen(mount_path) == 0) {
        printf("fixfs: Unable to mount device %s - no free paths found\n", device_path);
        return;
    }

    printf("fixfs: Found device %s not mounted at %s - proceed with fsck? (y/n)\n", device_path, mount_path);
    char response;
    scanf("%c", &response);

    if (response != 'y') {
        return;
    }

    mx_status_t r;

    fsck_options_t fsck_options = default_fsck_options;
    fsck_options.always_modify = true;

    if ((r = fsck(device_path, DISK_FORMAT_FAT, &fsck_options, launch_stdio_sync)) < 0) {
        fprintf(stderr, "fs_fsck: Failed to check device: %d\n", r);
        return;
    }

    int devfd;
    if ((devfd = open(device_path, O_RDWR)) < 0) {
        fprintf(stderr, "fixfs: Error opening block device %s\n", device_path);
        return;
    }

    mx_status_t status = mount(devfd, mount_path, DISK_FORMAT_FAT, mount_options, launch_logs_async);
    if (status != MX_OK) {
        fprintf(stderr, "fixfs: Error while mounting %s at %s: %d\n", device_path, mount_path, status);
    } else {
        printf("fixfs: Successfully mounted device %s at %s\n", device_path, mount_path);
    }

    close(devfd);
}

mx_status_t process_block_device(const char* device_name) {
    char device_path[PATH_MAX];
    disk_format_t disk_format;
    mount_options_t mount_options;

    snprintf(device_path, sizeof(device_path), "%s/%s", PATH_DEV_BLOCK, device_name);

    int devfd = open(device_path, O_RDONLY);

    if (devfd < 0) {
        printf("fixfs: Error opening block device %s\n", device_path);
        return MX_ERR_ACCESS_DENIED;
    }

    disk_format = detect_disk_format(devfd);
    mount_options = default_mount_options;

    uint8_t guid[GPT_GUID_LEN];
    ssize_t len = ioctl_block_get_type_guid(devfd, guid, sizeof(guid));

    char topo_path[PATH_MAX];
    ssize_t r = ioctl_device_get_topo_path(devfd, topo_path, sizeof(topo_path));

    if (r <= 0) {
        strcpy(topo_path, "UNKNOWN");
    }

    close(devfd);

    switch (disk_format) {
    case DISK_FORMAT_BLOBFS: {
        mount_options.create_mountpoint = true;
        check_and_remount(device_path, PATH_BLOBSTORE, disk_format, &mount_options);
        break;
    }
    case DISK_FORMAT_MINFS: {
        if (gpt_is_sys_guid(guid, len)) {
            mount_options.readonly = true;
            mount_options.wait_until_ready = true;
            mount_options.create_mountpoint = true;
            check_and_remount(device_path, PATH_SYSTEM, disk_format, &mount_options);
        } else if (gpt_is_data_guid(guid, len)) {
            mount_options.wait_until_ready = true;
            check_and_remount(device_path, PATH_DATA, disk_format, &mount_options);
        }
        break;
    }
    case DISK_FORMAT_FAT: {
        if (!gpt_is_efi_guid(guid, len)) {
            mount_options.readonly = false;
            mount_options.wait_until_ready = true;
            check_and_remount_fat(device_path, topo_path, &mount_options);
        }
        break;
    }
    default: {
        break;
    }
    }

    return MX_OK;
}

// This will only reformat the first matching device found for a particular mount path
int main(int argc, char** argv) {
    struct dirent* de;
    DIR* dir = opendir(PATH_DEV_BLOCK);

    if (!dir) {
        printf("fixfs: Error opening %s\n", PATH_DEV_BLOCK);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }

        if (process_block_device(de->d_name) != MX_OK) {
            return -1;
        }
    }

    closedir(dir);

    printf("fixfs: Done!\n");
    return 0;
}
