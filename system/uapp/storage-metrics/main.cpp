// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/types.h>

namespace {

using MinfsMetrics = fuchsia_minfs_Metrics;

int Usage() {
    fprintf(stdout, "usage: storage-metrics [ <option>* ]\n");
    fprintf(stdout, " storage-metrics reports metrics for storage components (block"
                    " devices and filesystems)\n");
    fprintf(stdout, " --block_device BLOCK_DEVICE : retrieves metrics for the block"
                    " device at the given path\n");
    fprintf(stdout, " --clear : clears metrics on block device given by block_device\n");
    fprintf(stdout, " --fs PATH : retrieves metrics for the filesystem at the given path\n");
    fprintf(stdout, " --enable_metrics=[true|false] : enables or disables metrics"
                    " for the filesystem given by path\n");
    fprintf(stdout, " --help : Show this help message\n");
    return -1;
}

void PrintFsStats(MinfsMetrics metrics) {
    printf("General IO metrics\n");
    printf("create calls:                       %lu\n", metrics.create_calls);
    printf("successful create calls:            %lu\n", metrics.create_calls_success);
    printf("create nanoseconds:                 %lu\n", metrics.create_ticks);
    printf("\n");

    printf("read calls:                         %lu\n", metrics.read_calls);
    printf("bytes read:                         %lu\n", metrics.read_size);
    printf("read nanoseconds:                   %lu\n", metrics.read_ticks);
    printf("\n");

    printf("write calls:                        %lu\n", metrics.write_calls);
    printf("bytes written:                      %lu\n", metrics.write_size);
    printf("write nanoseconds:                  %lu\n", metrics.write_ticks);
    printf("\n");

    printf("truncate calls:                     %lu\n", metrics.truncate_calls);
    printf("truncate nanoseconds:               %lu\n", metrics.truncate_ticks);
    printf("\n");

    printf("unlink calls:                       %lu\n", metrics.unlink_calls);
    printf("successful unlink calls:            %lu\n", metrics.unlink_calls_success);
    printf("unlink nanoseconds:                 %lu\n", metrics.unlink_ticks);
    printf("\n");

    printf("rename calls:                       %lu\n", metrics.rename_calls);
    printf("successful rename calls:            %lu\n", metrics.rename_calls_success);
    printf("rename nanoseconds:                 %lu\n", metrics.rename_ticks);
    printf("\n");

    printf("Vnode initialization metrics\n");
    printf("initialized VMOs:                   %lu\n", metrics.initialized_vmos);
    printf("initialized direct blocks:          %u\n", metrics.init_dnum_count);
    printf("initialized indirect blocks:        %u\n", metrics.init_inum_count);
    printf("initialized doubly indirect blocks: %u\n", metrics.init_dinum_count);
    printf("bytes of files initialized:         %lu\n", metrics.init_user_data_size);
    printf("ticks during initialization:        %lu\n", metrics.init_user_data_ticks);
    printf("\n");

    printf("Internal vnode open metrics\n");
    printf("vnodes opened:                      %lu\n", metrics.vnodes_opened);
    printf("vnodes open cache hits:             %lu\n", metrics.vnodes_opened_cache_hit);
    printf("vnode open nanoseconds:             %lu\n", metrics.vnode_open_ticks);
    printf("\n");

    printf("Internal vnode lookup metrics\n");
    printf("lookup calls:                       %lu\n", metrics.lookup_calls);
    printf("successful lookup calls:            %lu\n", metrics.lookup_calls_success);
    printf("lookup nanoseconds:                 %lu\n", metrics.lookup_ticks);
}

zx_status_t EnableFsStats(const char* path, bool enable) {
    fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status;
    fzl::FdioCaller caller(fbl::move(fd));
    zx_status_t rc = fuchsia_minfs_MinfsToggleMetrics(caller.borrow_channel(), enable, &status);
    if (rc != ZX_OK || status != ZX_OK) {
        fprintf(stderr, "Error toggling metrics for %s, errno %d (%s)\n",
                path, errno, strerror(errno));
        return (rc) ? rc : status;
    }
    return status;
}

zx_status_t GetFsStats(const char* path) {
    fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status;
    fzl::FdioCaller caller(fbl::move(fd));
    MinfsMetrics metrics;
    zx_status_t rc = fuchsia_minfs_MinfsGetMetrics(caller.borrow_channel(), &status, &metrics);
    if (status == ZX_ERR_UNAVAILABLE) {
        fprintf(stderr, "Metrics Unavailable for %s, errno %d (%s)\n",
                path, errno, strerror(errno));
        return status;
    } else if (rc != ZX_OK || status != ZX_OK) {
        fprintf(stderr, "Error getting metrics for %s, errno %d (%s)\n",
                path, errno, strerror(errno));
        return (rc != ZX_OK) ? rc : status;
    }
    PrintFsStats(metrics);
    return status;
}

zx_status_t GetBlockStats(const char* dev, bool clear) {
    fbl::unique_fd fd(open(dev, O_RDONLY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", dev, errno, strerror(errno));
        return ZX_ERR_IO;
    }

    block_stats_t stats;
    zx_status_t rc = static_cast<zx_status_t>(ioctl_block_get_stats(fd.get(), &clear, &stats));
    if (rc != ZX_OK) {
        fprintf(stderr, "Error getting stats for %s, errno %d (%s)\n", dev, errno, strerror(errno));
        return rc;
    }

    printf(R"(total submitted block ops:      %zu
total submitted blocks:         %zu
total submitted read ops:       %zu
total submitted blocks read:    %zu
total submitted write ops:      %zu
total submitted blocks written: %zu)",
           stats.total_ops, stats.total_blocks, stats.total_reads,
           stats.total_blocks_read, stats.total_writes, stats.total_blocks_written);
    printf("\n");
    return rc;
}

} //namespace

int main(int argc, char** argv) {
    fbl::StringBuffer<PATH_MAX> blkdev;
    fbl::StringBuffer<PATH_MAX> fs;
    bool clear = false;
    bool enable = true;
    bool check_enable = false;
    static const struct option opts[] = {
        {"block_device", required_argument, NULL, 'b'},
        {"clear", no_argument, NULL, 'c'},
        {"fs", required_argument, NULL, 'f'},
        {"enable_metrics", required_argument, NULL, 'e'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };
    for (int opt; (opt = getopt_long(argc, argv, "", opts, nullptr)) != -1;) {
        switch (opt) {
        case 'b':
            blkdev.Append(optarg);
            break;
        case 'c':
            clear = (strlen(optarg) == 0 || strcmp(optarg, "true") == 0);
            break;
        case 'f':
            fs.Append(optarg);
            break;
        case 'e':
            check_enable = true;
            enable = (strlen(optarg) == 0 || strcmp(optarg, "true") == 0);
            break;
        case 'h':
            __FALLTHROUGH;
        default:
            return Usage();
        }
    }
    if (!blkdev.empty()) {
        if (GetBlockStats(blkdev.c_str(), clear) != ZX_OK) {
            return -1;
        }
    }
    if (!fs.empty()) {
        // The order of these conditionals allows for stats to be output regardless of the
        // value of enable.
        if (check_enable && enable) {
            if(EnableFsStats(fs.c_str(), enable) != ZX_OK) {
                return -1;
            }
        }
        if (GetFsStats(fs.c_str()) != ZX_OK) {
            return -1;
        }
        if (check_enable && !enable) {
            if(EnableFsStats(fs.c_str(), enable) != ZX_OK) {
                return -1;
            }
        }
    }
    return 0;
}