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
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <utility>

namespace {

using MinfsMetrics = fuchsia_minfs_Metrics;

int Usage() {
    fprintf(stdout, "usage: storage-metrics [ <option>* ] [paths]\n");
    fprintf(stdout, " storage-metrics reports metrics for storage components (block"
                    " devices and filesystems). It is currently limited to minfs\n");
    fprintf(stdout, " --clear : clears metrics on block devices supporting paths\n");
    fprintf(stdout, " --enable_metrics=[true|false] : enables or disables metrics"
                    " for the filesystems supporting path\n");
    fprintf(stdout, " --help : Show this help message\n");
    return -1;
}

// Type to track whether whether a boolean flag without a default value has been set
enum class BooleanFlagState { kUnset,
                              kEnable,
                              kDisable };

struct StorageMetricOptions {
    // True indicates that a call to retrieve block device metrics should also clear those metrics.
    bool clear_block = false;
    // Value passed to a filesystem toggle metrics request.
    BooleanFlagState enable_fs_metrics = BooleanFlagState::kUnset;
};

void PrintFsMetrics(const MinfsMetrics& metrics, const char* path) {
    printf("Filesystem Metrics for: %s\n", path);
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

// Sends a FIDL call to enable or disable filesystem metrics for path
zx_status_t EnableFsMetrics(const char* path, bool enable) {
    fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status;
    fzl::FdioCaller caller(std::move(fd));
    zx_status_t rc = fuchsia_minfs_MinfsToggleMetrics(caller.borrow_channel(), enable, &status);
    if (rc != ZX_OK || status != ZX_OK) {
        fprintf(stderr, "Error toggling metrics for %s, status %d\n",
                path, (rc != ZX_OK) ? rc : status);
        return (rc != ZX_OK) ? rc : status;
    }
    return status;
}

// Retrieves the Filesystem metrics for path. Only supports Minfs.
zx_status_t GetFsMetrics(const char* path, MinfsMetrics* out_metrics) {
    fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status;
    fzl::FdioCaller caller(std::move(fd));
    zx_status_t rc = fuchsia_minfs_MinfsGetMetrics(caller.borrow_channel(), &status, out_metrics);
    if (status == ZX_ERR_UNAVAILABLE) {
        fprintf(stderr, "Metrics Unavailable for %s\n", path);
        return status;
    } else if (rc != ZX_OK || status != ZX_OK) {
        fprintf(stderr, "Error getting metrics for %s, status %d\n",
                path, (rc != ZX_OK) ? rc : status);
        return (rc != ZX_OK) ? rc : status;
    }
    return status;
}

void PrintBlockMetrics(const char* dev, const block_stats_t& stats) {
    printf(R"(
Block Metrics for device path: %s 
total submitted block ops:      %zu
total submitted blocks:         %zu
total submitted read ops:       %zu
total submitted blocks read:    %zu
total submitted write ops:      %zu
total submitted blocks written: %zu
)",
           dev, stats.total_ops, stats.total_blocks, stats.total_reads,
           stats.total_blocks_read, stats.total_writes, stats.total_blocks_written);
}

// Retrieves metrics for the block device at dev. Clears metrics if clear is true.
zx_status_t GetBlockMetrics(const char* dev, bool clear, block_stats_t* stats) {
    fbl::unique_fd fd(open(dev, O_RDONLY));
    if (!fd) {
        fprintf(stderr, "Error opening %s, errno %d (%s)\n", dev, errno, strerror(errno));
        return ZX_ERR_IO;
    }
    ssize_t rc = ioctl_block_get_stats(fd.get(), &clear, stats);
    if (rc < 0) {
        fprintf(stderr, "Error getting stats for %s\n", dev);
        return static_cast<zx_status_t>(rc);
    }
    return ZX_OK;
}

void ParseCommandLineArguments(int argc, char** argv, StorageMetricOptions* options) {
    static const struct option opts[] = {
        {"clear", optional_argument, NULL, 'c'},
        {"enable_metrics", optional_argument, NULL, 'e'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };
    for (int opt; (opt = getopt_long(argc, argv, "c::e::h", opts, nullptr)) != -1;) {
        switch (opt) {
        case 'c':
            options->clear_block = (optarg == nullptr || strcmp(optarg, "true") == 0);
            break;
        case 'e':
            options->enable_fs_metrics = (optarg == nullptr || strcmp(optarg, "true") == 0)
                                             ? BooleanFlagState::kEnable
                                             : BooleanFlagState::kDisable;
            break;
        case 'h':
            __FALLTHROUGH;
        default:
            Usage();
        }
    }
}

// Retrieves filesystem metrics for the filesystem at path and prints them.
void RunFsMetrics(const fbl::StringBuffer<PATH_MAX> path, const StorageMetricOptions options) {
    fbl::unique_fd fd(open(path.c_str(), O_RDONLY | O_ADMIN));
    if (!fd) {
        fd.reset(open(path.c_str(), O_RDONLY));
        if (!fd) {
            fprintf(stderr, "storage-metrics could not open target: %s, errno %d (%s)\n",
                    path.c_str(), errno, strerror(errno));
            return;
        }
    }

    fuchsia_io_FilesystemInfo info;
    zx_status_t status;
    fzl::FdioCaller caller(std::move(fd));
    zx_status_t io_status = fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(),
                                                                     &status, &info);
    if (io_status != ZX_OK || status != ZX_OK) {
        fprintf(stderr, "storage-metrics could not open %s, status %d\n",
                path.c_str(), (io_status != ZX_OK) ? io_status : status);
        return;
    }

    // Skip any filesystems that aren't minfs
    info.name[fuchsia_io_MAX_FS_NAME_BUFFER - 1] = '\0';
    const char* name = reinterpret_cast<const char*>(info.name);
    if (strcmp(name, "minfs") != 0) {
        fprintf(stderr, "storage-metrics does not support filesystem type %s\n", name);
        return;
    }

    zx_status_t rc;
    // The order of these conditionals allows for stats to be output regardless of the
    // value of enable.
    if (options.enable_fs_metrics == BooleanFlagState::kEnable) {
        rc = EnableFsMetrics(path.c_str(), true);
        if (rc != ZX_OK) {
            fprintf(stderr, "storage-metrics could not enable filesystem metrics for %s,"
                            " status %d\n",
                    path.c_str(), rc);
            return;
        }
    }
    MinfsMetrics metrics;
    rc = GetFsMetrics(path.c_str(), &metrics);
    if (rc == ZX_OK) {
        PrintFsMetrics(metrics, path.c_str());
    } else {
        fprintf(stderr, "storage-metrics could not get filesystem metrics for %s,"
                        " status %d\n",
                path.c_str(), rc);
        return;
    }
    if (options.enable_fs_metrics == BooleanFlagState::kDisable) {
        rc = EnableFsMetrics(path.c_str(), false);
        if (rc != ZX_OK) {
            fprintf(stderr, "storage-metrics could not disable filesystem metrics for %s,"
                            " status %d\n",
                    path.c_str(), rc);
        }
    }
}

// Retrieves and prints metrics for the block device associated with the filesystem at path.
void RunBlockMetrics(const fbl::StringBuffer<PATH_MAX> path, const StorageMetricOptions options) {
    fbl::unique_fd fd(open(path.c_str(), O_RDONLY | O_ADMIN));
    if (!fd) {
        fd.reset(open(path.c_str(), O_RDONLY));
        if (!fd) {
            fprintf(stderr, "storage-metrics could not open target: %s, errno %d (%s)\n",
                    path.c_str(), errno, strerror(errno));
            return;
        }
    }

    char device_buffer[1024];
    size_t path_len;
    zx_status_t status;
    fzl::FdioCaller caller(std::move(fd));
    zx_status_t io_status = fuchsia_io_DirectoryAdminGetDevicePath(caller.borrow_channel(), &status,
                                                                   device_buffer,
                                                                   sizeof(device_buffer) - 1,
                                                                   &path_len);
    const char* device_path = nullptr;
    if (io_status == ZX_OK && status == ZX_OK) {
        device_buffer[path_len] = '\0';
        device_path = device_buffer;
    }

    zx_status_t rc;
    block_stats_t stats;
    if (device_path != nullptr) {
        rc = GetBlockMetrics(device_path, options.clear_block, &stats);
        if (rc == ZX_OK) {
            PrintBlockMetrics(device_path, stats);
        } else {
            fprintf(stderr, "storage-metrics could not retrieve block metrics for %s,"
                            " status %d\n",
                    path.c_str(), rc);
        }
    } else {
        fprintf(stderr, "storage-metrics could not get the block device for %s\n",
                path.c_str());
    }
}

} //namespace

int main(int argc, char** argv) {
    StorageMetricOptions options;
    ParseCommandLineArguments(argc, argv, &options);
    // Iterate through the remaining arguments, which are all paths
    for (int i = optind; i < argc; i++) {
        fbl::StringBuffer<PATH_MAX> path;
        path.Append(argv[i]);

        printf("Metrics for: %s\n", path.c_str());
        RunFsMetrics(path, options);
        RunBlockMetrics(path, options);
        printf("\n");
    }

    return 0;
}
