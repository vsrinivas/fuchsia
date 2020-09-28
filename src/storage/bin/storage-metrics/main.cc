// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <fuchsia/storage/metrics/c/fidl.h>
#include <getopt.h>
#include <lib/fdio/cpp/caller.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <storage-metrics/block-metrics.h>
#include <storage-metrics/fs-metrics.h>

#include "src/storage/minfs/metrics.h"

namespace {

using MinfsFidlMetrics = ::llcpp::fuchsia::minfs::Metrics;

int Usage() {
  fprintf(stdout, "usage: storage-metrics [ <option>* ] [paths]\n");
  fprintf(stdout,
          " storage-metrics reports metrics for storage components (block"
          " devices and filesystems). It is currently limited to minfs\n");
  fprintf(stdout, " --clear : clears metrics on block devices supporting paths\n");
  fprintf(stdout,
          " --enable_metrics=[true|false] : enables or disables metrics"
          " for the filesystems supporting path\n");
  fprintf(stdout, " --help : Show this help message\n");
  return -1;
}

// Type to track whether whether a boolean flag without a default value has been set
enum class BooleanFlagState { kUnset, kEnable, kDisable };

struct StorageMetricOptions {
  // True indicates that a call to retrieve block device metrics should also clear those metrics.
  bool clear_block = false;
  // Value passed to a filesystem toggle metrics request.
  BooleanFlagState enable_fs_metrics = BooleanFlagState::kUnset;
};

void PrintFsMetrics(const MinfsFidlMetrics& metrics, const char* path) {
  minfs::MinfsMetrics minfs_metrics(&metrics);
  printf("Filesystem Metrics for: %s\n", path);
  printf("General IO metrics\n");
  minfs_metrics.Dump(stdout, true);
  minfs_metrics.Dump(stdout, false);
  // minfs_metrics.Dump(stdout);
  printf("\n");
}

// Sends a FIDL call to enable or disable filesystem metrics for path
zx_status_t EnableFsMetrics(const char* path, bool enable) {
  fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
  if (!fd) {
    fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
    return ZX_ERR_IO;
  }

  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t rc = fuchsia_minfs_MinfsToggleMetrics(caller.borrow_channel(), enable, &status);
  if (rc != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "Error toggling metrics for %s, status %d\n", path,
            (rc != ZX_OK) ? rc : status);
    return (rc != ZX_OK) ? rc : status;
  }
  return status;
}

// Retrieves the Filesystem metrics for path. Only supports Minfs.
zx_status_t GetFsMetrics(const char* path, MinfsFidlMetrics* out_metrics) {
  fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
  if (!fd) {
    fprintf(stderr, "Error opening %s, errno %d (%s)\n", path, errno, strerror(errno));
    return ZX_ERR_IO;
  }

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = llcpp::fuchsia::minfs::Minfs::Call::GetMetrics(caller.channel());
  if (!result.ok()) {
    fprintf(stderr, "Error getting metrics for %s, status %d\n", path, result.status());
    return result.status();
  }
  if (result.value().status == ZX_ERR_UNAVAILABLE) {
    fprintf(stderr, "Metrics Unavailable for %s\n", path);
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    fprintf(stderr, "Error getting metrics for %s, status %d\n", path, result.value().status);
    return result.value().status;
  }
  if (!result.value().metrics) {
    fprintf(stderr, "Error getting metrics for %s, returned metrics was null\n", path);
    return ZX_ERR_INTERNAL;
  }
  *out_metrics = *result.value().metrics;
  return ZX_OK;
}

void PrintBlockMetrics(const char* dev, const fuchsia_hardware_block_BlockStats& stats) {
  printf("Block Metrics for device path: %s\n", dev);
  storage_metrics::BlockDeviceMetrics metrics(&stats);
  metrics.Dump(stdout);
}

// Retrieves metrics for the block device at dev. Clears metrics if clear is true.
zx_status_t GetBlockMetrics(const char* dev, bool clear, fuchsia_hardware_block_BlockStats* stats) {
  fbl::unique_fd fd(open(dev, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Error opening %s, errno %d (%s)\n", dev, errno, strerror(errno));
    return ZX_ERR_IO;
  }
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t status;
  zx_status_t io_status =
      fuchsia_hardware_block_BlockGetStats(caller.borrow_channel(), clear, &status, stats);
  if (io_status != ZX_OK) {
    status = io_status;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "Error getting stats for %s\n", dev);
    return status;
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
      fprintf(stderr, "storage-metrics could not open target: %s, errno %d (%s)\n", path.c_str(),
              errno, strerror(errno));
      return;
    }
  }

  fuchsia_io_FilesystemInfo info;
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t io_status =
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &info);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "storage-metrics could not open %s, status %d\n", path.c_str(),
            (io_status != ZX_OK) ? io_status : status);
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
      fprintf(stderr,
              "storage-metrics could not enable filesystem metrics for %s,"
              " status %d\n",
              path.c_str(), rc);
      return;
    }
  }
  MinfsFidlMetrics metrics;
  rc = GetFsMetrics(path.c_str(), &metrics);
  if (rc == ZX_OK) {
    PrintFsMetrics(metrics, path.c_str());
  } else {
    fprintf(stderr,
            "storage-metrics could not get filesystem metrics for %s,"
            " status %d\n",
            path.c_str(), rc);
    return;
  }
  if (options.enable_fs_metrics == BooleanFlagState::kDisable) {
    rc = EnableFsMetrics(path.c_str(), false);
    if (rc != ZX_OK) {
      fprintf(stderr,
              "storage-metrics could not disable filesystem metrics for %s,"
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
      fprintf(stderr, "storage-metrics could not open target: %s, errno %d (%s)\n", path.c_str(),
              errno, strerror(errno));
      return;
    }
  }

  char device_buffer[1024];
  size_t path_len;
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t io_status = fuchsia_io_DirectoryAdminGetDevicePath(
      caller.borrow_channel(), &status, device_buffer, sizeof(device_buffer) - 1, &path_len);
  const char* device_path = nullptr;
  if (io_status == ZX_OK && status == ZX_OK) {
    device_buffer[path_len] = '\0';
    device_path = device_buffer;
  }

  zx_status_t rc;
  fuchsia_hardware_block_BlockStats stats;
  if (device_path != nullptr) {
    rc = GetBlockMetrics(device_path, options.clear_block, &stats);
    if (rc == ZX_OK) {
      PrintBlockMetrics(device_path, stats);
    } else {
      fprintf(stderr,
              "storage-metrics could not retrieve block metrics for %s,"
              " status %d\n",
              path.c_str(), rc);
    }
  } else {
    // Maybe this is not a filesystem. See if this happens to be a block device.
    // TODO(auradkar): We need better args parsing to consider fs and block
    // device seperately.
    rc = GetBlockMetrics(path.c_str(), options.clear_block, &stats);
    if (rc != ZX_OK) {
      fprintf(stderr,
              "storage-metrics could not retrieve block metrics for %s,"
              " status %d\n",
              path.c_str(), rc);
    }
    PrintBlockMetrics(path.c_str(), stats);
  }
}

}  // namespace

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
