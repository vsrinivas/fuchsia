// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
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

#include <utility>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fs/trace.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>
#include <trace-provider/provider.h>

namespace {

int Fsck(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  if (options.readonly_after_initialization) {
    return Fsck(std::move(bc), minfs::Repair::kDisabled);
  }
  return Fsck(std::move(bc), minfs::Repair::kEnabled);
}

int Mount(std::unique_ptr<block_client::BlockDevice> device, const minfs::MountOptions& options) {
  zx::channel root(zx_take_startup_handle(FS_HANDLE_ROOT_ID));
  if (!root) {
    FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
    return ZX_ERR_BAD_STATE;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto loop_quit = [&loop]() {
    loop.Quit();
    FS_TRACE_WARN("minfs: Unmounted\n");
  };

  zx_status_t status = MountAndServe(options, loop.dispatcher(), std::move(device), std::move(root),
                                     std::move(loop_quit));
  if (status != ZX_OK) {
    if (options.verbose) {
      FS_TRACE_ERROR("minfs: Failed to mount: %d\n", status);
    }
    return -1;
  }

  if (options.verbose) {
    FS_TRACE_ERROR("minfs: Mounted successfully\n");
  }

  loop.Run();
  return 0;
}

int Mkfs(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
  return Mkfs(options, bc.get());
}

struct {
  const char* name;
  int (*func)(std::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions&);
  uint32_t flags;
  const char* help;
} CMDS[] = {
    {"create", Mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", Mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", Fsck, O_RDONLY, "check filesystem integrity"},
    {"fsck", Fsck, O_RDONLY, "check filesystem integrity"},
};

int usage() {
  fprintf(stderr,
          "usage: minfs [ <option>* ] <command> [ <arg>* ]\n"
          "\n"
          "options:\n"
          "    -v|--verbose                  Some debug messages\n"
          "    -r|--readonly                 Mount filesystem read-only (after repair)\n"
          "    -j|--journal                  Enable journaling for writeback\n"
          "    -m|--metrics                  Collect filesystem metrics\n"
          "    -s|--fvm_data_slices SLICES   When mkfs on top of FVM,\n"
          "                                  preallocate |SLICES| slices of data. \n"
          "    -h|--help                     Display this message\n"
          "\n"
          "On Fuchsia, MinFS takes the block device argument by handle.\n"
          "This can make 'minfs' commands hard to invoke from command line.\n"
          "Try using the [mkfs,fsck,mount,umount] commands instead\n"
          "\n");
  for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
    fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", CMDS[n].name, CMDS[n].help);
  }
  fprintf(stderr, "%9s %-10s %s\n", "", "mount", "mount filesystem");
  fprintf(stderr, "\n");
  return -1;
}

}  // namespace

int main(int argc, char** argv) {
  minfs::MountOptions options;
  options.use_journal = false;

  while (1) {
    static struct option opts[] = {
        {"readonly", no_argument, nullptr, 'r'},
        {"metrics", no_argument, nullptr, 'm'},
        {"journal", no_argument, nullptr, 'j'},
        {"verbose", no_argument, nullptr, 'v'},
        {"fvm_data_slices", required_argument, nullptr, 's'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rmjvhs:", opts, &opt_index);
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
      case 'j':
        options.use_journal = true;
        break;
      case 'v':
        options.verbose = true;
        break;
      case 's':
        options.fvm_data_slices = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'h':
      default:
        return usage();
    }
  }

  argc -= optind;
  argv += optind;

  if (argc != 1) {
    return usage();
  }
  char* cmd = argv[0];

  // Block device passed by handle
  zx::channel device_channel = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status = block_client::RemoteBlockDevice::Create(std::move(device_channel), &device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: Could not access block device\n");
    return -1;
  }

  if (strcmp(cmd, "mount") == 0) {
    return Mount(std::move(device), options);
  }

  std::unique_ptr<minfs::Bcache> bc;

  bool readonly_device = false;
  if (CreateBcache(std::move(device), &readonly_device, &bc) != ZX_OK) {
    fprintf(stderr, "minfs: error: cannot create block cache\n");
    return -1;
  }
  options.readonly_after_initialization |= readonly_device;
  options.repair_filesystem &= !readonly_device;

  for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
    if (strcmp(cmd, CMDS[i].name) == 0) {
      int r = CMDS[i].func(std::move(bc), options);
      if (options.verbose) {
        fprintf(stderr, "minfs: %s completed with result: %d\n", cmd, r);
      }
      return r;
    }
  }
  return -1;
}
