// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <optional>
#include <utility>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/factory/factoryfs/fsck.h"
#include "src/storage/factory/factoryfs/mkfs.h"
#include "src/storage/factory/factoryfs/mount.h"

namespace {

using block_client::BlockDevice;
using block_client::RemoteBlockDevice;

zx_status_t Mount(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  return factoryfs::Mount(std::move(device), options,
                          fidl::ServerEnd<fuchsia_io::Directory>(
                              zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST))));
}

zx_status_t Mkfs(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  return factoryfs::FormatFilesystem(device.get());
}

zx_status_t Fsck(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  return factoryfs::Fsck(std::move(device), options);
}

using CommandFunction = zx_status_t (*)(std::unique_ptr<BlockDevice>, factoryfs::MountOptions*);

const struct {
  const char* name;
  CommandFunction func;
  const char* help;
} kCmds[] = {
    {"create", Mkfs, "initialize filesystem"},     {"mkfs", Mkfs, "initialize filesystem"},
    {"check", Fsck, "check filesystem integrity"}, {"fsck", Fsck, "check filesystem integrity"},
    {"mount", Mount, "mount filesystem"},
};

zx_status_t usage() {
  fprintf(stderr,
          "usage: factoryfs [ <options>* ] <command> [ <arg>* ]\n"
          "\n"
          "options: -v|--verbose   Additional debug logging\n"
          "         -h|--help                  Display this message\n"
          "\n"
          "On Fuchsia, factoryfs takes the block device argument by handle.\n"
          "This can make 'factoryfs' commands hard to invoke from command line.\n"
          "Try using the [mkfs,fsck,mount,umount] commands instead\n"
          "\n");

  for (unsigned n = 0; n < (sizeof(kCmds) / sizeof(kCmds[0])); n++) {
    fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", kCmds[n].name, kCmds[n].help);
  }
  fprintf(stderr, "\n");
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t ProcessArgs(int argc, char** argv, CommandFunction* func,
                        factoryfs::MountOptions* options) {
  while (true) {
    static struct option opts[] = {
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "vmh", opts, &opt_index);

    if (c < 0) {
      break;
    }
    switch (c) {
      case 'v':
        options->verbose = true;
        break;
      case 'h':
      default:
        break;
        return usage();
    }
  }

  argc -= optind;
  argv += optind;

  if (argc < 1) {
    return usage();
  }
  const char* command = argv[0];

  // Validate command
  for (const auto& cmd : kCmds) {
    if (!strcmp(command, cmd.name)) {
      *func = cmd.func;
    }
  }

  if (*func == nullptr) {
    fprintf(stderr, "Unknown command: %s\n", command);
    return usage();
  }

  return ZX_OK;
}
}  // namespace

int main(int argc, char** argv) {
  CommandFunction func = nullptr;
  factoryfs::MountOptions options;
  zx_status_t status = ProcessArgs(argc, argv, &func, &options);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }

  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FX_LOGS(ERROR) << "Could not access startup handle to block device";
    return EXIT_FAILURE;
  }

  fbl::unique_fd svc_fd(open("/svc", O_RDONLY));
  if (!svc_fd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open svc from incoming namespace";
    return EXIT_FAILURE;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  status = RemoteBlockDevice::Create(
      fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_connection)), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    return EXIT_FAILURE;
  }
  status = func(std::move(device), &options);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  return 0;
}
