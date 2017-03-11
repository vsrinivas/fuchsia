// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>

#include "filesystems.h"

const char* test_root_path;
char test_disk_path[PATH_MAX];
fs_info_t* test_info;

#define RAMCTL_PATH "/dev/misc/ramctl"

int create_ramdisk(const char* ramdisk_name, char* ramdisk_path_out) {
    if (strlen(ramdisk_name) + strlen(RAMCTL_PATH) + 1 >= PATH_MAX) {
        return -1;
    }
    strcpy(ramdisk_path_out, RAMCTL_PATH);
    strcat(ramdisk_path_out, "/");
    strcat(ramdisk_path_out, ramdisk_name);
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
        return fd;
    }
    ramdisk_ioctl_config_t config;
    config.blk_size = 512;
    config.blk_count = (1 << 22);
    strcpy(config.name, ramdisk_name);
    ssize_t r = ioctl_ramdisk_config(fd, &config);
    if (r != NO_ERROR) {
        fprintf(stderr, "Could not configure ramdev\n");
        return -1;
    }
    close(fd);

    // TODO(smklein): Remove once MG-468 is resolved
    usleep(10000);
    return 0;
}

int destroy_ramdisk(const char* ramdisk_path) {
    int fd = open(ramdisk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    ssize_t r = ioctl_ramdisk_unlink(fd);
    if (r != NO_ERROR) {
        fprintf(stderr, "Could not shut off ramdisk\n");
        return -1;
    }
    if (close(fd) < 0) {
        fprintf(stderr, "Could not close ramdisk fd\n");
        return -1;
    }
    return 0;
}

int setup_fs_test(void) {
    test_root_path = MOUNT_PATH;
    int r = mkdir(test_root_path, 0755);
    if ((r < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystem\n");
        return -1;
    }

    if (create_ramdisk("fs-test-ramdisk", test_disk_path)) {
        fprintf(stderr, "[FAILED]: Could not create ramdisk for test\n");
        exit(-1);
    }
    if (test_info->mkfs(test_disk_path)) {
        fprintf(stderr, "[FAILED]: Could not format ramdisk for test\n");
        exit(-1);
    }

    if (test_info->mount(test_disk_path, test_root_path)) {
        fprintf(stderr, "[FAILED]: Error mounting filesystem\n");
        exit(-1);
    }
    return 0;
}

int teardown_fs_test(void) {
    if (test_info->unmount(test_root_path)) {
        fprintf(stderr, "[FAILED]: Error unmounting filesystem\n");
        exit(-1);
    }

    if (destroy_ramdisk(test_disk_path)) {
        fprintf(stderr, "[FAILED]: Error destroying ramdisk\n");
        exit(-1);
    }

    return 0;
}

// FS-specific functionality:

int mkfs_memfs(const char* disk_path) {
    return 0;
}

// TODO(smklein): Even this hacky solution has a hacky implementation, and
// should be replaced with a variation of "rm -r" when ready.
static int unlink_recursive(const char* path) {
    DIR* dir;
    if ((dir = opendir(path)) == NULL) {
        return errno;
    }

    struct dirent* de;
    int r = 0;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        char tmp[PATH_MAX];
        tmp[0] = 0;
        int bytes_left = PATH_MAX - 1;
        strncat(tmp, path, bytes_left);
        bytes_left -= strlen(path);
        strncat(tmp, "/", bytes_left);
        bytes_left--;
        strncat(tmp, de->d_name, bytes_left);
        // At the moment, we don't have a great way of identifying what is /
        // isn't a directory. Just try to open it as a directory, and return
        // without an error if we're wrong.
        if ((r = unlink_recursive(tmp)) < 0) {
            break;
        }
        if ((r = unlink(tmp)) < 0) {
            break;
        }
    }

    closedir(dir);
    return r;
}

// TODO(smklein): It would be cleaner to unmount the filesystem completely,
// and remount a fresh copy. However, a hackier (but currently working)
// solution involves recursively deleting all files in the mounted
// filesystem.
int mount_memfs(const char* disk_path, const char* mount_path) {
    struct stat st;
    if (stat(test_root_path, &st)) {
        int fd = mkdir(test_root_path, 0644);
        if (fd < 0) {
            return -1;
        }
        close(fd);
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    int r = unlink_recursive(test_root_path);
    return r;
}

int unmount_memfs(const char* mount_path) {
    return unlink_recursive(test_root_path);
}

int mkfs_minfs(const char* disk_path) {
    mx_status_t status;
    if ((status = mkfs(disk_path, DISK_FORMAT_MINFS, launch_stdio_sync)) != NO_ERROR) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }
    return 0;
}

int mount_minfs(const char* disk_path, const char* mount_path) {
    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open disk: %s\n", disk_path);
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is ready to accept
    // commands.
    mx_status_t status;
    if ((status = mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async)) != NO_ERROR) {
        fprintf(stderr, "Could not mount filesystem\n");
        return status;
    }

    return 0;
}

int unmount_minfs(const char* mount_path) {
    mx_status_t status = umount(mount_path);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to unmount filesystem\n");
        return status;
    }
    return 0;
}

fs_info_t FILESYSTEMS[NUM_FILESYSTEMS] = {
    {"memfs", mkfs_memfs, mount_memfs, unmount_memfs, false, true, true },
    {"minfs", mkfs_minfs, mount_minfs, unmount_minfs,  true, true, true },
};
