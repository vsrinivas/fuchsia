// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/block.h>
#include <fs-management/mount.h>

// Path to mounted filesystem currently being tested
const char* test_root_path;

// TODO(smklein): Even this hacky solution has a hacky implementation, and
// should be replaced with a variation of "rm -r" when ready.
int unlink_recursive(const char* path) {
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
int mount_hack(void) {
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

int mount_memfs(void) {
    return mount_hack();
}

int unmount_memfs(void) {
    return unlink_recursive(test_root_path);
}

#define RAMCTL_PATH "/dev/misc/ramctl"
#define RAMDISK_NAME "fs-test-ramdisk"
#define RAMDISK_PATH (RAMCTL_PATH "/" RAMDISK_NAME)
#define MOUNT_PATH "/tmp/magenta-fs-test"

int mount_minfs(void) {
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
        return fd;
    }
    ramdisk_ioctl_config_t config;
    config.blk_size = 512;
    config.blk_count = (1 << 20);
    strcpy(config.name, RAMDISK_NAME);
    ssize_t r = ioctl_block_ramdisk_config(fd, &config);
    if (r != NO_ERROR) {
        fprintf(stderr, "Could not configure ramdev\n");
        return -1;
    }
    close(fd);

    // TODO(smklein): Remove once MG-468 is resolved
    usleep(100);

    mx_status_t status;
    if ((status = mkfs(RAMDISK_PATH, DISK_FORMAT_MINFS, launch_stdio_sync)) != NO_ERROR) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }

    fd = open(RAMDISK_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is ready to accept
    // commands.
    if ((status = mount(fd, MOUNT_PATH, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async)) != NO_ERROR) {
        fprintf(stderr, "Could not mount filesystem\n");
        return status;
    }

    return 0;
}

int unmount_minfs(void) {
    mx_status_t status = umount(MOUNT_PATH);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to unmount filesystem\n");
        return status;
    }
    int fd = open(RAMDISK_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    ssize_t r = ioctl_block_ramdisk_unlink(fd);
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

struct {
    const char* name;
    const char* mount_path;
    int (*mount)(void);
    int (*unmount)(void);
} FILESYSTEMS[] = {
    {"memfs", MOUNT_PATH, mount_memfs, unmount_memfs},
    {"minfs", MOUNT_PATH, mount_minfs, unmount_minfs},
};

int test_append(void);
int test_basic(void);
int test_attr(void);
int test_directory(void);
int test_maxfile(void);
int test_overflow(void);
int test_rw_workers(void);
int test_rename(void);
int test_sync(void);
int test_truncate(void);
int test_unlink(void);

struct {
    const char* name;
    int (*test)(void);
} FS_TESTS[] = {
    {"append", test_append},
    {"basic", test_basic},
    {"attr", test_attr},
    {"directory", test_directory},
    {"maxfile", test_maxfile},
    {"overflow", test_overflow},
    {"rw_workers", test_rw_workers},
    {"rename", test_rename},
    {"sync", test_sync},
    {"truncate", test_truncate},
    {"unlink", test_unlink},
};

void run_fs_tests(int (*mount)(void), int (*unmount)(void), int argc, char** argv) {
    fprintf(stderr, "--- fs tests ---\n");
    for (unsigned i = 0; i < countof(FS_TESTS); i++) {
        if (argc > 1 && strcmp(argv[1], FS_TESTS[i].name)) {
            continue;
        }
        fprintf(stderr, "Running Test: %s\n", FS_TESTS[i].name);
        if (mount()) {
            fprintf(stderr, "FAILED: Error mounting filesystem\n");
            exit(-1);
        }

        if (FS_TESTS[i].test()) {
            fprintf(stderr, "FAILED: %s\n", FS_TESTS[i].name);
            exit(-1);
        } else {
            fprintf(stderr, "PASSED: %s\n", FS_TESTS[i].name);
        }

        if (unmount()) {
            fprintf(stderr, "FAILED: Error unmounting filesystem\n");
            exit(-1);
        }
    }
}

int main(int argc, char** argv) {
    int r = mkdir(MOUNT_PATH, 0755);
    if ((r < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystems\n");
        return -1;
    }

    for (unsigned i = 0; i < countof(FILESYSTEMS); i++) {
        printf("Testing FS: %s\n", FILESYSTEMS[i].name);
        test_root_path = FILESYSTEMS[i].mount_path;
        run_fs_tests(FILESYSTEMS[i].mount, FILESYSTEMS[i].unmount, argc, argv);
    }
    return 0;
}
