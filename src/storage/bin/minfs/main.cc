// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <memory>
#include <vector>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/mount.h"

namespace {

const char* kComponentCommand = "component";

int Fsck(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& mount_options) {
  minfs::FsckOptions options;
  // If the disk is read only, pass that in.
  options.read_only = mount_options.writability == minfs::Writability::ReadOnlyDisk;
  // Only repair if we are fully writable.
  options.repair = mount_options.writability == minfs::Writability::Writable;
  return minfs::Fsck(std::move(bc), options).status_value();
}

// Run the filesystem server on top of the block device |device|.
// This function blocks until the filesystem server is instructed to exit.
int Mount(std::unique_ptr<minfs::Bcache> bcache, const minfs::MountOptions& options) {
  fidl::ServerEnd<fuchsia_io::Directory> root(
      zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  zx::result status = Mount(std::move(bcache), options, std::move(root));
  if (status.is_error()) {
    if (options.verbose) {
      FX_LOGS(ERROR) << "Failed to mount: " << status.status_string();
    }
    return EXIT_FAILURE;
  }

  return 0;
}

int Mkfs(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  return minfs::Mkfs(options, bc.get()).status_value();
}

int StartComponent(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  FX_LOGS(INFO) << "starting minfs component";

  // The arguments are either null or don't matter, we collect the real ones later on the startup
  // protocol. What does matter is the DIRECTORY_REQUEST so we can start serving that protocol.
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!outgoing_server.is_valid()) {
    FX_LOGS(ERROR) << "PA_DIRECTORY_REQUEST startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir(std::move(outgoing_server));

  zx::channel lifecycle_channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
  if (!lifecycle_channel.is_valid()) {
    FX_LOGS(ERROR) << "PA_LIFECYCLE startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request(
      std::move(lifecycle_channel));

  zx::result status = minfs::StartComponent(std::move(outgoing_dir), std::move(lifecycle_request));
  if (status.is_error()) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
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

  if (bc_or->is_read_only) {
    options->writability = minfs::Writability::ReadOnlyDisk;
    options->repair_filesystem = false;
  }
  *out_bcache = std::move(bc_or->bcache);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  syslog::SetLogSettings({}, {"minfs"});
  minfs::MountOptions options;

  const std::vector<Command> commands = {
      Command{kComponentCommand, StartComponent, "start the minfs component"},
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
        options.writability = minfs::Writability::ReadOnlyFilesystem;
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

  for (const auto& command : commands) {
    if (strcmp(cmd, command.name) == 0) {
      std::unique_ptr<minfs::Bcache> bc;
      if (strcmp(cmd, kComponentCommand) != 0) {
        // If we aren't being launched as a component, we are getting the block device as a startup
        // handle. Get it and create the bcache.
        zx::channel device_channel = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));

        std::unique_ptr<block_client::RemoteBlockDevice> device;
        zx_status_t status =
            block_client::RemoteBlockDevice::Create(std::move(device_channel), &device);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Could not access block device";
          return EXIT_FAILURE;
        }

        if (int ret = CreateBcacheUpdatingOptions(std::move(device), &options, &bc); ret != 0) {
          return ret;
        }
      }

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
