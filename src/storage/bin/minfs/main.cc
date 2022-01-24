// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/vfs.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"

namespace {

int Fsck(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  if (options.readonly_after_initialization) {
    return minfs::Fsck(std::move(bc), minfs::FsckOptions()).status_value();
  }
  return minfs::Fsck(std::move(bc), minfs::FsckOptions{.repair = true}).status_value();
}

// Run the filesystem server on top of the block device |device|.
// This function blocks until the filesystem server is instructed to exit.
int Mount(std::unique_ptr<minfs::Bcache> bcache, const minfs::MountOptions& options) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto on_unmount = [&loop]() {
    loop.Quit();
    FX_LOGS(WARNING) << "Unmounted";
  };

  auto fs_or = MountAndServe(options, loop.dispatcher(), std::move(bcache),
                             zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)),
                             std::move(on_unmount));
  if (fs_or.is_error()) {
    if (options.verbose) {
      FX_LOGS(ERROR) << "Failed to mount: " << fs_or.status_string();
    }
    return EXIT_FAILURE;
  }

  if (options.verbose) {
    FX_LOGS(ERROR) << "Mounted successfully";
  }

  // |ZX_ERR_CANCELED| is returned when the loop is cancelled via |loop.Quit()|.
  ZX_ASSERT(loop.Run() == ZX_ERR_CANCELED);
  return 0;
}

int Mkfs(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  return minfs::Mkfs(options, bc.get()).status_value();
}

struct Command {
  const char* name;
  std::function<int(std::unique_ptr<minfs::Bcache>, const minfs::MountOptions&)> func;
  const char* help;
};

int usage(const std::vector<Command>& commands) {
  fprintf(stderr,
          "usage: minfs [ <option>* ] <command> [ <arg>* ]\n"
          "\n"
          "options:\n"
          "    -v|--verbose                    Some debug messages\n"
          "    -r|--readonly                   Mount filesystem read-only (after repair)\n"
          "    -m|--metrics                    Collect filesystem metrics\n"
          "    -s|--fvm_data_slices SLICES     When mkfs on top of FVM,\n"
          "                                    preallocate |SLICES| slices of data. \n"
          "    --fsck_after_every_transaction  Run fsck after every transaction.\n"
          "    -h|--help                       Display this message\n"
          "\n"
          "On Fuchsia, MinFS takes the block device argument by handle.\n"
          "This can make 'minfs' commands hard to invoke from command line.\n"
          "Try using the [mkfs,fsck,mount] commands instead\n"
          "\n");
  bool first = true;
  for (const auto& command : commands) {
    fprintf(stderr, "%9s %-10s %s\n", first ? "commands:" : "", command.name, command.help);
    first = false;
  }
  fprintf(stderr, "\n");
  return EXIT_FAILURE;
}

// Creates a |minfs::Bcache| by consuming |device|.
// |options| are updated to reflect if the device is read-only.
int CreateBcacheUpdatingOptions(std::unique_ptr<block_client::RemoteBlockDevice> device,
                                minfs::MountOptions* options,
                                std::unique_ptr<minfs::Bcache>* out_bcache) {
  auto bc_or = minfs::CreateBcache(std::move(device));
  if (bc_or.is_error()) {
    fprintf(stderr, "minfs: error: cannot create block cache\n");
    FX_LOGS(ERROR) << "cannot create block cache";
    return EXIT_FAILURE;
  }

  options->readonly_after_initialization |= bc_or->is_read_only;
  options->repair_filesystem &= !bc_or->is_read_only;
  *out_bcache = std::move(bc_or->bcache);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  syslog::SetLogSettings({}, {"minfs"});
  minfs::MountOptions options;

  const std::vector<Command> commands = {
      Command{"create", Mkfs, "initialize filesystem"},
      Command{"mkfs", Mkfs, "initialize filesystem"},
      Command{"check", Fsck, "check filesystem integrity"},
      Command{"fsck", Fsck, "check filesystem integrity"},
      Command{"mount",
              [](std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
                return Mount(std::move(bc), options);
              },
              "mount and serve the filesystem"}};

  while (true) {
    static struct option opts[] = {
        {"readonly", no_argument, nullptr, 'r'},
        {"metrics", no_argument, nullptr, 'm'},
        {"verbose", no_argument, nullptr, 'v'},
        {"fvm_data_slices", required_argument, nullptr, 's'},
        {"fsck_after_every_transaction", no_argument, nullptr, 'f'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rmvs:h", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options.readonly_after_initialization = true;
        break;
      case 'm':
        options.metrics = true;
        break;
      case 'v':
        options.verbose = true;
        break;
      case 's':
        options.fvm_data_slices = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        break;
      case 'f':
        options.fsck_after_every_transaction = true;
        break;
      case 'h':
      default:
        return usage(commands);
    }
  }

  argc -= optind;
  argv += optind;

  if (argc != 1) {
    return usage(commands);
  }
  char* cmd = argv[0];

  // Block device passed by handle
  zx::channel device_channel = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status = block_client::RemoteBlockDevice::Create(std::move(device_channel), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not access block device";
    return EXIT_FAILURE;
  }

  std::unique_ptr<minfs::Bcache> bc;
  if (int ret = CreateBcacheUpdatingOptions(std::move(device), &options, &bc); ret != 0) {
    return ret;
  }

  for (const auto& command : commands) {
    if (strcmp(cmd, command.name) == 0) {
      int r = command.func(std::move(bc), options);
      if (options.verbose) {
        fprintf(stderr, "minfs: %s completed with result: %d\n", cmd, r);
      }
      return r;
    }
  }
  fprintf(stderr, "minfs: unknown command\n");
  usage(commands);
  return EXIT_FAILURE;
}
