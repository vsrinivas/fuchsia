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

int mount_minfs(void) {
    return mount_hack();
}

int unmount_minfs(void) {
    return unlink_recursive(test_root_path);
}

struct {
    const char* name;
    const char* mount_path;
    int (*mount)(void);
    int (*unmount)(void);
} FILESYSTEMS[] = {
    {"memfs", "/tmp/magenta-fs-test", mount_memfs, unmount_memfs},
    {"minfs", "/data/magenta-fs-test", mount_minfs, unmount_minfs},
};

int test_maxfile(void);
int test_rw_workers(void);
int test_basic(void);
int test_rename(void);
int test_append(void);
int test_truncate(void);

struct {
    const char* name;
    int (*test)(void);
} FS_TESTS[] = {
    {"append", test_append},
    {"maxfile", test_maxfile},
    {"rw_workers", test_rw_workers},
    {"basic", test_basic},
    {"rename", test_rename},
    {"truncate", test_truncate},
};

int run_fs_tests(int (*mount)(void), int (*unmount)(void), int argc, char** argv) {
    fprintf(stderr, "--- fs tests ---\n");
    int num_tests = sizeof(FS_TESTS) / sizeof(FS_TESTS[0]);
    int res = 0;
    for (int i = 0; i < num_tests; i++) {
        if (argc > 1 && strcmp(argv[1], FS_TESTS[i].name)) {
            continue;
        }
        fprintf(stderr, "Running Test: %s\n", FS_TESTS[i].name);
        if (mount()) {
            fprintf(stderr, "Error mounting filesystem\n");
            return -1;
        }

        int res_delta = FS_TESTS[i].test();
        if (res_delta) {
            fprintf(stderr, "FAILED: %s\n", FS_TESTS[i].name);
        } else {
            fprintf(stderr, "PASSED: %s\n", FS_TESTS[i].name);
        }
        res |= res_delta;

        if (unmount()) {
            fprintf(stderr, "Error unmounting filesystem\n");
            return -1;
        }
    }
    return res;
}

int main(int argc, char** argv) {
    int num_filesystems = sizeof(FILESYSTEMS) / sizeof(FILESYSTEMS[0]);
    int res = 0;
    for (int i = 0; i < num_filesystems; i++) {
        printf("Testing FS: %s\n", FILESYSTEMS[i].name);
        test_root_path = FILESYSTEMS[i].mount_path;
        res |= run_fs_tests(FILESYSTEMS[i].mount, FILESYSTEMS[i].unmount, argc, argv);
    }
    return res;
}
