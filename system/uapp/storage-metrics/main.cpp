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
#include <zircon/types.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>

namespace {

int Usage() {
    fprintf(stdout, "usage: storage-metrics [ <option>* ]\n");
    fprintf(stdout, " storage-metrics reports metrics for a block device\n");
    fprintf(stdout, " --block_device PATH : retrieves metrics for the block"
                    " device at the given path\n");
    fprintf(stdout, " --clear : clears metrics on block device given by block_device\n");
    fprintf(stdout, " --help : Show this help message\n");
    return -1;
}

zx_status_t CmdBlockStats(const char* dev, bool clear) {
    fbl::unique_fd fd(open(dev, O_RDONLY));
    if(!fd) {
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
total submitted blocks written: %zu)", stats.total_ops, stats.total_blocks, stats.total_reads,
    stats.total_blocks_read, stats.total_writes, stats.total_blocks_written);
    printf("\n");
    return rc;
}

} //namespace

int main(int argc, char** argv) {
    fbl::StringBuffer<PATH_MAX> path;
    bool clear = false;
    static const struct option opts[] = {
        {"block_device", required_argument, nullptr, 'b'},
        {"clear", no_argument, nullptr, 'c'},
        {"help", no_argument, nullptr, 'h'},
        {0, 0, 0, 0},
    };
    for (int opt; (opt = getopt_long(argc, argv, "", opts, nullptr)) != -1;) {
        switch (opt) {
        case 'b':
            path.Append(optarg);
            break;
        case 'c':
            clear = (strlen(optarg) == 0 || strcmp(optarg, "true") == 0);
            break;
        case 'h':
            __FALLTHROUGH;
        default:
            return Usage();
        }
    }
    if (!path.empty()) {
        if (CmdBlockStats(path.c_str(), clear) != ZX_OK) {
            return -1;
        }
    }
    return 0;
}