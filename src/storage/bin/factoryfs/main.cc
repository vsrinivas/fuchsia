// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
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

#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs/trace.h>
#include <fs/vfs.h>

#include "src/storage/factory/factoryfs/fsck.h"
#include "src/storage/factory/factoryfs/mkfs.h"
#include "src/storage/factory/factoryfs/mount.h"

namespace {

using block_client::BlockDevice;
using block_client::RemoteBlockDevice;

zx_status_t Mount(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  // TODO(fxb/34531): Support both methods (outgoing_server and root_server) till fixed.
  zx::channel root_server = zx::channel(zx_take_startup_handle(FS_HANDLE_ROOT_ID));
  zx::channel diagnostics_dir = zx::channel(zx_take_startup_handle(FS_HANDLE_DIAGNOSTICS_DIR));

  if (outgoing_server.is_valid() && root_server.is_valid()) {
    FS_TRACE_ERROR(
        "factoryfs: both PA_DIRECTORY_REQUEST and FS_HANDLE_ROOT_ID provided - need one or the "
        "other.\n");
    return ZX_ERR_BAD_STATE;
  }

  zx::channel export_root;
  factoryfs::ServeLayout layout;
  if (outgoing_server.is_valid()) {
    export_root = std::move(outgoing_server);
    layout = factoryfs::ServeLayout::kExportDirectory;
  } else if (root_server.is_valid()) {
    export_root = std::move(root_server);
    layout = factoryfs::ServeLayout::kDataRootOnly;
  } else {
    // neither provided or we can't access them for some reason.
    FS_TRACE_ERROR("factoryfs: could not get startup handle to serve on\n");
    return ZX_ERR_BAD_STATE;
  }

  return factoryfs::Mount(std::move(device), options, std::move(export_root), layout);
}

zx_status_t Mkfs(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  return factoryfs::FormatFilesystem(device.get());
}

zx_status_t Fsck(std::unique_ptr<BlockDevice> device, factoryfs::MountOptions* options) {
  return factoryfs::Fsck(std::move(device), options);
}

typedef zx_status_t (*CommandFunction)(std::unique_ptr<BlockDevice> device,
                                       factoryfs::MountOptions* options);

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
          "         -m|--metrics               Collect filesystem metrics\n"
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
  while (1) {
    static struct option opts[] = {
        {"verbose", no_argument, nullptr, 'v'},
        {"metrics", no_argument, nullptr, 'm'},
        {"journal", no_argument, nullptr, 'j'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "vmj:h", opts, &opt_index);

    if (c < 0) {
      break;
    }
    switch (c) {
      case 'm':
        options->metrics = true;
        break;
      case 'v':
        options->verbose = true;
        break;
      case 'j':
        // ignore journal.
        // TODO manalib Instead of ignoring this flag in the filesystem code,if the file
        // system doesn't support journaling, fs-host shouldn't set the flag.
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
  for (unsigned i = 0; i < sizeof(kCmds) / sizeof(kCmds[0]); i++) {
    if (!strcmp(command, kCmds[i].name)) {
      *func = kCmds[i].func;
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
    FS_TRACE_ERROR("factoryfs: Could not access startup handle to block device\n");
    return EXIT_FAILURE;
  }

  fbl::unique_fd svc_fd(open("/svc", O_RDONLY));
  if (!svc_fd.is_valid()) {
    FS_TRACE_ERROR("factoryfs: Failed to open svc from incoming namespace\n");
    return EXIT_FAILURE;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  status = RemoteBlockDevice::Create(std::move(block_connection), &device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Could not initialize block device\n");
    return EXIT_FAILURE;
  }
  status = func(std::move(device), &options);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  return 0;
}
