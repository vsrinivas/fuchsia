// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/device/vfs.h>

int usage(void) {
    fprintf(stderr, "usage: df [ <option>* ] [paths]\n");
    fprintf(stderr, "df displays the mounted filesystems for a list of paths\n");
    fprintf(stderr, " -i : List inode information instead of block usage\n");
    fprintf(stderr, " -h : Show sizes in human readable format (e.g., 1K 2M 3G)\n");
    fprintf(stderr, " --help : Show this help message\n");
    return -1;
}

typedef struct {
    bool node_usage;
    bool human_readable;
} df_options_t;

const char* root = "/";

int parse_args(int argc, const char** argv, df_options_t* options, const char*** dirs, size_t* count) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-i")) {
            options->node_usage = true;
        } else if (!strcmp(argv[1], "-h")) {
            options->human_readable = true;
        } else if (!strcmp(argv[1], "--help")) {
            return usage();
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc >= 2) {
        *dirs = &argv[1];
        *count = argc - 1;
    } else {
        *dirs = &root;
        *count = 1;
    }
    return 0;
}

// Format for the header
const char* hfmt = "%-10s %10s %10s %10s %3s%%  %-10s  %-10s\n";
// Format for the human-readable header
const char* hrfmt = "%-10s %5s %5s %5s %5s%%  %-10s  %-10s\n";
// Format for the individual filesystems queried
const char* ffmt = "%-10s %10lu %10lu %10lu %3lu%%  %-10s  %-10s\n";

#define KB (1lu << 10)
#define MB (1lu << 20)
#define GB (1lu << 30)
#define TB (1lu << 40)
#define PB (1lu << 50)
#define EB (1lu << 60)

// Conditionally print the size if it falls within the range of the magnitude.
// [1.0XX, 999XX]
bool print_magnitude(int padding, size_t size, size_t magnitude, const char* mag_string) {
    if (size < 10 * magnitude) {
        printf("%*lu.%lu%s ", padding - 4, size / magnitude,
               size / (magnitude / 10) % 10, mag_string);
        return true;
    } else if (size < magnitude << 10) {
        printf("%*lu%s ", padding - 2, size / magnitude, mag_string);
        return true;
    }
    return false;
}

void print_human_readable(int padding, size_t size) {
    if (size < KB) {
        printf("%*s ", padding, "0");
    } else if (print_magnitude(padding, size, KB, "KB")) {
    } else if (print_magnitude(padding, size, MB, "MB")) {
    } else if (print_magnitude(padding, size, GB, "GB")) {
    } else if (print_magnitude(padding, size, TB, "TB")) {
    } else if (print_magnitude(padding, size, PB, "PB")) {
    } else {
        printf("%*lu ", padding, size);
    }
}

void print_fs_type(const char* name, const df_options_t* options,
                   const vfs_query_info_t* info, const int name_len,
                   const char* device_path) {
    if (options->node_usage) {
        size_t nodes_total = info ? info->total_nodes : 0;
        size_t nodes_used = info ? info->used_nodes : 0;
        size_t nodes_available = nodes_total - nodes_used;
        size_t use_percentage = nodes_total ? nodes_used * 100 / nodes_total : 0;
        printf(ffmt,
               info != NULL ? info->name : "?",
               nodes_total,
               nodes_used,
               nodes_available,
               use_percentage,
               name,
               device_path ? device_path : "none");
    } else {
        // Block Usage
        if (options->human_readable) {
            size_t bytes_total = info ? info->total_bytes: 0;
            size_t bytes_used = info ? info->used_bytes : 0;
            size_t bytes_available = bytes_total - bytes_used;
            size_t use_percentage = bytes_total ? bytes_used * 100 / bytes_total : 0;
            printf("%-10s ", info != NULL ? info->name : "?");
            print_human_readable(5, bytes_total);
            print_human_readable(5, bytes_used);
            print_human_readable(5, bytes_available);
            printf("%5lu%%  ", use_percentage);
            printf("%-10s  ", name);
            printf("%-10s\n", device_path ? device_path : "none");
        } else {
            size_t blocks_total = info ? info->total_bytes >> 10 : 0;
            size_t blocks_used = info ? info->used_bytes >> 10 : 0;
            size_t blocks_available = blocks_total - blocks_used;
            size_t use_percentage = blocks_total ? blocks_used * 100 / blocks_total : 0;
            printf(ffmt,
                   info != NULL ? info->name : "?",
                   blocks_total,
                   blocks_used,
                   blocks_available,
                   use_percentage,
                   name,
                   device_path ? device_path : "none");
        }
    }

}
typedef union {
    vfs_query_info_t info;
    struct {
        alignas(vfs_query_info_t) char h[sizeof(vfs_query_info_t)];
        char name[MAX_FS_NAME_LEN + 1];
    };
} vfs_query_info_wrapper_t;

int main(int argc, const char** argv) {
    const char** dirs;
    size_t dircount;
    df_options_t options;
    memset(&options, 0, sizeof(df_options_t));
    int r;
    if ((r = parse_args(argc, argv, &options, &dirs, &dircount))) {
        return r;
    }

    if (options.node_usage) {
        printf(hfmt, "Filesystem", "Inodes", "IUsed", "IFree", "IUse",
               "Path", "Device");
    } else {
        if (options.human_readable) {
            printf(hrfmt, "Filesystem", "Size", "Used", "Avail", "Use",
                   "Path", "Device");
        } else {
            printf(hfmt, "Filesystem", "1K-Blocks", "Used", "Available", "Use",
                   "Path", "Device");
        }
    }

    int flags = O_RDONLY | O_ADMIN;

    // Try to open path with O_ADMIN so we can query for underlying block devices.
    // If we fail, open directory without O_ADMIN. Block devices will not be returned.
    for (size_t i = 0; i < dircount; i++) {
        int fd;
        if ((fd = open(dirs[i], flags)) < 0) {
            flags ^= O_ADMIN;

            if ((fd = open(dirs[i], flags)) < 0) {
                fprintf(stderr, "df: Could not open target\n");
                return -1;
            }

            fprintf(stderr, "df: Unable to acquire admin access to target\n");
        }

        vfs_query_info_wrapper_t wrapper;
        char device_path[1024];
        ssize_t r = ioctl_vfs_query_fs(fd, &wrapper.info, sizeof(wrapper) - 1);
        int name_len = r - sizeof(vfs_query_info_t);

        if (name_len > 0) {
            wrapper.name[name_len] = '\0';
        }

        ssize_t s = ioctl_vfs_get_device_path(fd, device_path, sizeof(device_path));
        print_fs_type(dirs[i], &options, name_len > 0 ? &wrapper.info : NULL, name_len, (s > 0 ? device_path : NULL));
        close(fd);
    }

    return 0;
}
