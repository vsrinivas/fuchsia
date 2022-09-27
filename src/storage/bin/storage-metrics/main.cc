// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <getopt.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
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

#include "src/storage/fshost/constants.h"

namespace {

namespace fio = fuchsia_io;

int Usage() {
  fprintf(stdout, "usage: storage-metrics [ <option>* ] [paths]\n");
  fprintf(stdout, "storage-metrics reports metrics for block devices\n");
  fprintf(stdout, " --clear : clears metrics on block devices supporting paths\n");
  fprintf(stdout, " --help : Show this help message\n");
  return -1;
}

struct StorageMetricOptions {
  // True indicates that a call to retrieve block device metrics should also clear those metrics.
  bool clear_block = false;
};

void PrintBlockMetrics(const char* dev, const fuchsia_hardware_block::wire::BlockStats& stats) {
  printf("Block Metrics for device path: %s\n", dev);
  storage_metrics::BlockDeviceMetrics metrics(&stats);
  metrics.Dump(stdout);
}

// Retrieves metrics for the block device at dev. Clears metrics if clear is true.
zx::status<fuchsia_hardware_block::wire::BlockStats> GetBlockStats(const char* dev, bool clear) {
  auto client_end = component::Connect<fuchsia_hardware_block::Block>(dev);
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
      {"clear", optional_argument, nullptr, 'c'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, no_argument, nullptr, 0},
  };
  for (int opt; (opt = getopt_long(argc, argv, "c::e::h", opts, nullptr)) != -1;) {
    switch (opt) {
      case 'c':
        options->clear_block = (optarg == nullptr || strcmp(optarg, "true") == 0);
        break;
      case 'h':
        __FALLTHROUGH;
      default:
        Usage();
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
  if (result.ok() && result.value().s == ZX_OK) {
    std::string fshost_path(fshost::kHubAdminServicePath);
    auto fshost_or = component::Connect<fuchsia_fshost::Admin>(fshost_path.c_str());
    if (fshost_or.is_error()) {
      fprintf(stderr, "Error connecting to fshost (@ %s): %s\n", fshost_path.c_str(),
              fshost_or.status_string());
    } else {
      auto path_result = fidl::WireCall(*fshost_or)->GetDevicePath(result.value().info->fs_id);
      if (path_result.ok() && path_result->is_ok()) {
        device_path = std::string(path_result->value()->path.get());
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
    } else {
      PrintBlockMetrics(path, *block_stats);
    }
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
    RunBlockMetrics(path, options);
    printf("\n");
  }

  return 0;
}
