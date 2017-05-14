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
#include <fs-management/ramdisk.h>
#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>

#include "filesystems.h"

const char* test_root_path;
bool use_real_disk = false;
char test_disk_path[PATH_MAX];
fs_info_t* test_info;

const fsck_options_t test_fsck_options = {
    .verbose = false,
    .never_modify = true,
    .always_modify = false,
    .force = true,
};

int setup_fs_test(void) {
    test_root_path = MOUNT_PATH;
    int r = mkdir(test_root_path, 0755);
    if ((r < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystem\n");
        return -1;
    }

    if (!use_real_disk) {
        if (create_ramdisk("fs-test", test_disk_path, 512, (1 << 23))) {
            fprintf(stderr, "[FAILED]: Could not create ramdisk for test\n");
            exit(-1);
        }
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

    if (test_info->fsck(test_disk_path)) {
        fprintf(stderr, "[FAILED]: Filesystem fsck failed\n");
        exit(-1);
    }

    if (!use_real_disk) {
        if (destroy_ramdisk(test_disk_path)) {
            fprintf(stderr, "[FAILED]: Error destroying ramdisk\n");
            exit(-1);
        }
    }

    return 0;
}

// FS-specific functionality:

bool always_exists(void) { return true; }

int mkfs_memfs(const char* disk_path) {
    return 0;
}

int fsck_memfs(const char* disk_path) {
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
    if ((status = mkfs(disk_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                       &default_mkfs_options)) != NO_ERROR) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }
    return 0;
}

int fsck_minfs(const char* disk_path) {
    mx_status_t status;
    if ((status = fsck(disk_path, DISK_FORMAT_MINFS, &test_fsck_options, launch_stdio_sync)) != NO_ERROR) {
        fprintf(stderr, "fsck on MinFS failed");
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

bool thinfs_exists(void) {
    struct stat buf;
    return stat("/system/bin/thinfs", &buf) == 0;
}

int mkfs_thinfs(const char* disk_path) {
    mx_status_t status;
    if ((status = mkfs(disk_path, DISK_FORMAT_FAT, launch_stdio_sync,
                       &default_mkfs_options)) != NO_ERROR) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }
    return 0;
}

int fsck_thinfs(const char* disk_path) {
    mx_status_t status;
    if ((status = fsck(disk_path, DISK_FORMAT_FAT, &test_fsck_options, launch_stdio_sync)) != NO_ERROR) {
        fprintf(stderr, "fsck on FAT failed");
        return -1;
    }
    return 0;
}

int mount_thinfs(const char* disk_path, const char* mount_path) {
    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open disk: %s\n", disk_path);
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is ready to accept
    // commands.
    mx_status_t status;
    if ((status = mount(fd, mount_path, DISK_FORMAT_FAT, &default_mount_options,
                        launch_stdio_async)) != NO_ERROR) {
        fprintf(stderr, "Could not mount filesystem\n");
        return status;
    }

    return 0;
}

int unmount_thinfs(const char* mount_path) {
    mx_status_t status = umount(mount_path);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to unmount filesystem\n");
        return status;
    }
    return 0;
}

fs_info_t FILESYSTEMS[NUM_FILESYSTEMS] = {
    {"memfs",
        always_exists, mkfs_memfs, mount_memfs, unmount_memfs, fsck_memfs,
        .can_be_mounted = false,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = true,
        .nsec_granularity = 1,
    },
    {"minfs",
        always_exists, mkfs_minfs, mount_minfs, unmount_minfs, fsck_minfs,
        .can_be_mounted = true,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = false,
        .nsec_granularity = 1,
    },
    {"thinfs",
        thinfs_exists, mkfs_thinfs, mount_thinfs, unmount_thinfs, fsck_thinfs,
        .can_be_mounted = true,
        .can_mount_sub_filesystems = false,
        .supports_hardlinks = false,
        .supports_watchers = false,
        .nsec_granularity = MX_SEC(2),
    },
};
