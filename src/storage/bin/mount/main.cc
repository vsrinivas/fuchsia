// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <getopt.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <filesystem>

#include <fbl/unique_fd.h>

#include "src/storage/fshost/constants.h"

int usage(void) {
  fprintf(stderr,
          "usage: mount [ <option>* ] devicepath <mount-path>\n"
          "options:\n"
          " -r|--readonly  : Open the filesystem as read-only\n"
          " -v|--verbose   : Verbose mode\n"
          " --fshost-path  : The path to the fshost admin service (if different from the default)\n"
          " -h|--help      : Display this message\n"
          "\n"
          "Filesystems can only be mounted in /mnt/...\n");
  return EXIT_FAILURE;
}

int parse_args(int argc, char** argv, fidl::AnyArena& arena,
               fuchsia_fshost::wire::MountOptions& options, char** devicepath,
               std::string* mount_name, std::string& fshost_path) {
  while (true) {
    static struct option opts[] = {
        {"readonly", no_argument, nullptr, 'r'},          {"verbose", no_argument, nullptr, 'v'},
        {"compression", required_argument, nullptr, 'c'}, {"help", no_argument, nullptr, 'h'},
        {"fshost-path", required_argument, nullptr, 'p'}, {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rmvc:h", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options.set_read_only(true);
        break;
      case 'v':
        options.set_verbose(true);
        break;
      case 'c':
        options.set_write_compression_algorithm(arena, arena, optarg);
        break;
      case 'p':
        fshost_path = optarg;
        break;
      case 'h':
      default:
        return usage();
    }
  }

  argc -= optind;
  argv += optind;

  if (argc < 2) {
    return usage();
  }
  *devicepath = argv[0];

  std::filesystem::path path(argv[1]);
  if (path.parent_path() != "/mnt") {
    std::error_code error;
    auto directory = std::filesystem::canonical(path.parent_path(), error);
    if (error) {
      fprintf(stderr, "Bad mount path: %s\n\n", strerror(error.value()));
      return usage();
    }
    if (directory != "/mnt") {
      fprintf(stderr, "Only mounts in /mnt are supported.\n\n");
      return usage();
    }
  }
  *mount_name = path.filename();
  return 0;
}

int main(int argc, char** argv) {
  char* devicepath;
  std::string mount_name;
  std::string fshost_path(fshost::kHubAdminServicePath);
  fidl::Arena arena;
  fuchsia_fshost::wire::MountOptions options(arena);
  if (int r = parse_args(argc, argv, arena, options, &devicepath, &mount_name, fshost_path)) {
    return r;
  }

  if (options.has_verbose() && options.verbose()) {
    printf("fs_mount: Mounting device [%s] on path [/mnt/%s]\n", devicepath, mount_name.c_str());
  }

  auto block_device_or = component::Connect<fuchsia_hardware_block::Block>(devicepath);
  if (block_device_or.is_error()) {
    fprintf(stderr, "Error opening block device: %s\n", block_device_or.status_string());
    return EXIT_FAILURE;
  }

  auto fshost_or = component::Connect<fuchsia_fshost::Admin>(fshost_path.c_str());
  if (fshost_or.is_error()) {
    fprintf(stderr, "Error connecting to fshost (@ %s): %s\n", fshost_path.c_str(),
            fshost_or.status_string());
    return EXIT_FAILURE;
  }

  auto result =
      fidl::WireCall(*fshost_or)
          ->Mount(std::move(*block_device_or), fidl::StringView::FromExternal(mount_name), options);
  if (!result.ok()) {
    fprintf(stderr, "Error mounting, fidl error: %s\n", result.FormatDescription().c_str());
    return EXIT_FAILURE;
  }
  if (result->is_error()) {
    fprintf(stderr, "Error mounting: %s\n", zx_status_get_string(result->error_value()));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
