// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/device/vfs.h>

int usage(void) {
    fprintf(stderr, "usage: lsfs [ <option>* ] [directory = CWD]\n");
    fprintf(stderr, "lsfs displays the mounted filesystems in a directory\n");
    fprintf(stderr, " -s  : Show size usage of filesystem\n");
    fprintf(stderr, " -n  : Show node usage of filesystem\n");
    fprintf(stderr, " -b  : Show block device underlying filesystem\n");
    return -1;
}

typedef struct {
    bool size_usage;
    bool node_usage;
    bool block_device;
} lsfs_options_t;

int parse_args(int argc, char** argv, lsfs_options_t* options, const char** dirpath) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-s")) {
            options->size_usage = true;
        } else if (!strcmp(argv[1], "-n")) {
            options->node_usage = true;
        } else if (!strcmp(argv[1], "-b")) {
            options->block_device = true;
        } else if (!strcmp(argv[1], "-h")) {
            return usage();
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc >= 2) {
        *dirpath = argv[1];
    } else {
        *dirpath = ".";
    }
    return 0;
}

void print_fs_type(const char* name, const lsfs_options_t* options,
                   const vfs_query_info_t* info, const char* device_path) {
    printf("%-15s  ", name);
    printf("%-10s  ", info != NULL ? info->name : "?");
    if (options->size_usage) {
        printf("Bytes: [%lu / %lu] ", info != NULL ? info->used_bytes : 0,
                                      info != NULL ? info->total_bytes : 0);
    }
    if (options->node_usage) {
        printf("Nodes: [%lu / %lu] ", info != NULL ? info->used_nodes : 0,
                                      info != NULL ? info->total_nodes : 0);
    }
    if (options->block_device && device_path != NULL) {
        printf("%s", device_path);
    }
    printf("\n");
}

int main(int argc, char** argv) {
    const char* dirpath;
    lsfs_options_t options;
    memset(&options, 0, sizeof(lsfs_options_t));
    int r;
    if ((r = parse_args(argc, argv, &options, &dirpath))) {
        return r;
    }

    int flags = O_RDONLY | O_ADMIN;

    // Try to open directory with O_ADMIN so we can query for underlying block devices.
    // If we fail, open directory without O_ADMIN. Block devices will not be returned.
    int dirfd;
    if ((dirfd = open(dirpath, flags)) < 0) {
        flags ^= O_ADMIN;

        if ((dirfd = open(dirpath, flags)) < 0) {
            fprintf(stderr, "lsfs: Could not open target directory\n");
            return -1;
        }

        fprintf(stderr, "lsfs: Unable to acquire admin access to target directory\n");
    }

    DIR* dir;
    if ((dir = fdopendir(dirfd)) == NULL) {
        fprintf(stderr, "lsfs: Could not open target directory\n");
        return -1;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        int fd = openat(dirfd, de->d_name, flags);
        if (fd < 0) {
            printf("lsfs: couldn't open: %s\n", de->d_name);
            continue;
        }
        vfs_query_info_t info;
        char device_path[1024];
        ssize_t r = ioctl_vfs_query_fs(fd, &info, sizeof(info));
        ssize_t s = ioctl_vfs_get_device_path(fd, device_path, sizeof(device_path));
        print_fs_type(de->d_name, &options, (r == sizeof(info)) ? &info : NULL, (s > 0 ? device_path : NULL));
        close(fd);
    }
    closedir(dir);
    close(dirfd);
    return 0;
}
