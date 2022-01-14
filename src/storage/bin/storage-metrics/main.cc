// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.minfs/cpp/wire.h>
#include <getopt.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/service/llcpp/service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <string>
#include <utility>

#include <fbl/unique_fd.h>
#include <storage-metrics/block-metrics.h>
#include <storage-metrics/fs-metrics.h>

#include "src/storage/fshost/constants.h"
#include "src/storage/minfs/metrics.h"

namespace {

using MinfsFidlMetrics = fuchsia_minfs::wire::Metrics;
namespace fio = fuchsia_io;

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
zx::status<> EnableFsMetrics(const char* path, bool enable) {
  auto client_end = service::Connect<fuchsia_minfs::Minfs>(path);
  if (!client_end.is_ok()) {
    return client_end.take_error();
  }
  fidl::WireSyncClient client(std::move(client_end.value()));

  auto result = client->ToggleMetrics(enable);
  zx_status_t status = !result.ok() ? result.status() : result.value().status;
  if (status != ZX_OK) {
    fprintf(stderr, "Error toggling metrics for %s: %s\n", path, zx_status_get_string(status));
    return zx::error(status);
  }
  return zx::ok();
}

// Retrieves the Filesystem metrics for path. Only supports Minfs.
zx::status<MinfsFidlMetrics> GetFsMetrics(const char* path) {
  auto client_end = service::Connect<fuchsia_minfs::Minfs>(path);
  if (!client_end.is_ok()) {
    return client_end.take_error();
  }
  fidl::WireSyncClient client(std::move(client_end.value()));

  auto result = client->GetMetrics();
  zx_status_t status = !result.ok() ? result.status() : result.value().status;
  if (status != ZX_OK) {
    fprintf(stderr, "Error getting metrics for %s: %s\n", path, zx_status_get_string(status));
    return zx::error(status);
  }
  if (!result.value().metrics) {
    fprintf(stderr, "Error getting metrics for %s, returned metrics was null\n", path);
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(*result.value().metrics);
}

void PrintBlockMetrics(const char* dev, const fuchsia_hardware_block::wire::BlockStats& stats) {
  printf("Block Metrics for device path: %s\n", dev);
  storage_metrics::BlockDeviceMetrics metrics(&stats);
  metrics.Dump(stdout);
}

// Retrieves metrics for the block device at dev. Clears metrics if clear is true.
zx::status<fuchsia_hardware_block::wire::BlockStats> GetBlockStats(const char* dev, bool clear) {
  auto client_end = service::Connect<fuchsia_hardware_block::Block>(dev);
  if (!client_end.is_ok()) {
    return client_end.take_error();
  }
  fidl::WireSyncClient client(std::move(client_end.value()));

  auto result = client->GetStats(clear);
  zx_status_t status = !result.ok() ? result.error().status() : result.value().status;

  if (status != ZX_OK) {
    fprintf(stderr, "Error getting stats for %s: %s\n", dev, zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(*result.value().stats);
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
void RunFsMetrics(const char* path, const StorageMetricOptions options) {
  // The order of these conditionals allows for stats to be output regardless of the
  // value of enable.
  if (options.enable_fs_metrics == BooleanFlagState::kEnable) {
    zx::status<> rc = EnableFsMetrics(path, true);
    if (!rc.is_ok()) {
      fprintf(stderr, "storage-metrics could not enable filesystem metrics for %s: %s\n", path,
              rc.status_string());
      return;
    }
  }
  zx::status<MinfsFidlMetrics> metrics = GetFsMetrics(path);
  if (metrics.is_ok()) {
    PrintFsMetrics(*metrics, path);
  } else {
    fprintf(stderr,
            "storage-metrics could not get filesystem metrics for %s: %s. This may mean "
            "that it is not a minfs file system.\n",
            path, metrics.status_string());
    return;
  }
  if (options.enable_fs_metrics == BooleanFlagState::kDisable) {
    zx::status<> rc = EnableFsMetrics(path, false);
    if (!rc.is_ok()) {
      fprintf(stderr, "storage-metrics could not disable filesystem metrics for %s: %s\n", path,
              rc.status_string());
    }
  }
}

// Retrieves and prints metrics for the block device associated with the filesystem at path.
void RunBlockMetrics(const char* path, const StorageMetricOptions options) {
  fbl::unique_fd fd(open(path, O_RDONLY));
  if (!fd) {
    fd.reset(open(path, O_RDONLY));
    if (!fd) {
      fprintf(stderr, "storage-metrics could not open target: %s, errno %d (%s)\n", path, errno,
              strerror(errno));
      return;
    }
  }

  std::string device_path;
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fidl::WireCall(caller.directory())->QueryFilesystem();
  if (result.ok() && result->s == ZX_OK) {
    std::string fshost_path(fshost::kHubAdminServicePath);
    auto fshost_or = service::Connect<fuchsia_fshost::Admin>(fshost_path.c_str());
    if (fshost_or.is_error()) {
      fprintf(stderr, "Error connecting to fshost (@ %s): %s\n", fshost_path.c_str(),
              fshost_or.status_string());
    } else {
      auto path_result = fidl::WireCall(*fshost_or)->GetDevicePath(result->info->fs_id);
      if (path_result.ok() && path_result->result.is_response()) {
        device_path = std::string(path_result->result.response().path.get());
      }
    }
  }

  if (!device_path.empty()) {
    zx::status<fuchsia_hardware_block::wire::BlockStats> block_stats =
        GetBlockStats(device_path.data(), options.clear_block);
    if (block_stats.is_ok()) {
      PrintBlockMetrics(device_path.data(), *block_stats);
    } else {
      fprintf(stderr, "storage-metrics could not retrieve block metrics for %s: %s\n", path,
              block_stats.status_string());
    }
  } else {
    // Maybe this is not a filesystem. See if this happens to be a block device.
    // TODO(auradkar): We need better args parsing to consider fs and block
    // device seperately.
    zx::status<fuchsia_hardware_block::wire::BlockStats> block_stats =
        GetBlockStats(path, options.clear_block);
    if (!block_stats.is_ok()) {
      fprintf(stderr, "storage-metrics could not retrieve block metrics for %s: %s\n", path,
              block_stats.status_string());
    }
    PrintBlockMetrics(path, *block_stats);
  }
}

}  // namespace

int main(int argc, char** argv) {
  StorageMetricOptions options;
  ParseCommandLineArguments(argc, argv, &options);
  // Iterate through the remaining arguments, which are all paths
  for (int i = optind; i < argc; i++) {
    char* path = argv[i];

    printf("Metrics for: %s\n", path);
    RunFsMetrics(path, options);
    RunBlockMetrics(path, options);
    printf("\n");
  }

  return 0;
}
