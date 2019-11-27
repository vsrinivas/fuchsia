// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <getopt.h>
#include <lib/zx/channel.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <utility>

#include <blobfs/fsck.h>
#include <blobfs/mkfs.h>
#include <blobfs/mount.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <fs/trace.h>
#include <fs/vfs.h>

namespace {

using block_client::BlockDevice;
using block_client::RemoteBlockDevice;

zx_status_t Mount(std::unique_ptr<BlockDevice> device, blobfs::MountOptions* options) {
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  // TODO(fxb/34531): this currently supports both the old (data root only) and the new (outgoing
  // directory) behaviors. once all clients are moved over to using the new behavior, delete the old
  // one.
  zx::channel root_server = zx::channel(zx_take_startup_handle(FS_HANDLE_ROOT_ID));

  if (outgoing_server.is_valid() && root_server.is_valid()) {
    FS_TRACE_ERROR(
        "blobfs: both PA_DIRECTORY_REQUEST and FS_HANDLE_ROOT_ID provided - need one or the "
        "other.\n");
    return ZX_ERR_BAD_STATE;
  }

  zx::channel export_root;
  blobfs::ServeLayout layout;
  if (outgoing_server.is_valid()) {
    export_root = std::move(outgoing_server);
    layout = blobfs::ServeLayout::kExportDirectory;
  } else if (root_server.is_valid()) {
    export_root = std::move(root_server);
    layout = blobfs::ServeLayout::kDataRootOnly;
  } else {
    // neither provided? or we can't access them for some reason.
    FS_TRACE_ERROR("blobfs: could not get startup handle to serve on\n");
    return ZX_ERR_BAD_STATE;
  }

  return blobfs::Mount(std::move(device), options, std::move(export_root), layout);
}

zx_status_t Mkfs(std::unique_ptr<BlockDevice> device, blobfs::MountOptions* options) {
  return blobfs::FormatFilesystem(device.get());
}

zx_status_t Fsck(std::unique_ptr<BlockDevice> device, blobfs::MountOptions* options) {
  return blobfs::Fsck(std::move(device), options);
}

typedef zx_status_t (*CommandFunction)(std::unique_ptr<BlockDevice> device,
                                       blobfs::MountOptions* options);

const struct {
  const char* name;
  CommandFunction func;
  const char* help;
} kCmds[] = {
    {"create", Mkfs, "initialize filesystem"},     {"mkfs", Mkfs, "initialize filesystem"},
    {"check", Fsck, "check filesystem integrity"}, {"fsck", Fsck, "check filesystem integrity"},
    {"mount", Mount, "mount filesystem"},
};

int usage() {
  fprintf(stderr,
          "usage: blobfs [ <options>* ] <command> [ <arg>* ]\n"
          "\n"
          "options: -r|--readonly  Mount filesystem read-only\n"
          "         -m|--metrics   Collect filesystem metrics\n"
          "         -j|--journal   Utilize the blobfs journal\n"
          "                        For fsck, the journal is replayed before verification\n"
          "         -h|--help      Display this message\n"
          "\n"
          "On Fuchsia, blobfs takes the block device argument by handle.\n"
          "This can make 'blobfs' commands hard to invoke from command line.\n"
          "Try using the [mkfs,fsck,mount,umount] commands instead\n"
          "\n");
  for (unsigned n = 0; n < (sizeof(kCmds) / sizeof(kCmds[0])); n++) {
    fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", kCmds[n].name, kCmds[n].help);
  }
  fprintf(stderr, "\n");
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t ProcessArgs(int argc, char** argv, CommandFunction* func,
                        blobfs::MountOptions* options) {
  while (1) {
    static struct option opts[] = {
        {"readonly", no_argument, nullptr, 'r'}, {"metrics", no_argument, nullptr, 'm'},
        {"journal", no_argument, nullptr, 'j'},  {"pager", no_argument, nullptr, 'p'},
        {"help", no_argument, nullptr, 'h'},     {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rmjph", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options->writability = blobfs::Writability::ReadOnlyFilesystem;
        break;
      case 'm':
        options->metrics = true;
        break;
      case 'j':
        options->journal = true;
        break;
      case 'p':
        options->pager = true;
        break;
      case 'h':
      default:
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
  blobfs::MountOptions options;
  zx_status_t status = ProcessArgs(argc, argv, &func, &options);
  if (status != ZX_OK) {
    return -1;
  }

  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FS_TRACE_ERROR("blobfs: Could not access startup handle to block device\n");
    return -1;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  status = RemoteBlockDevice::Create(std::move(block_connection), &device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Could not initialize block device\n");
    return -1;
  }
  status = func(std::move(device), &options);
  if (status != ZX_OK) {
    return -1;
  }
  return 0;
}
