// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fs-management/mount.h>

int usage(void) {
  fprintf(stderr,
          "usage: mount [ <option>* ] devicepath mountpath\n"
          "options: \n"
          " -r|--readonly  : Open the filesystem as read-only\n"
          " -m|--metrics   : Collect filesystem metrics\n"
          " -v|--verbose   : Verbose mode\n"
          " -h|--help      : Display this message\n");
  return EXIT_FAILURE;
}

int parse_args(int argc, char** argv, fs_management::MountOptions* options, char** devicepath,
               char** mountpath) {
  while (1) {
    static struct option opts[] = {
        {"readonly", no_argument, NULL, 'r'}, {"metrics", no_argument, NULL, 'm'},
        {"verbose", no_argument, NULL, 'v'},  {"compression", required_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},     {NULL, 0, NULL, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rmvc:h", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options->readonly = true;
        break;
      case 'm':
        options->collect_metrics = true;
        break;
      case 'v':
        options->verbose_mount = true;
        break;
      case 'c':
        options->write_compression_algorithm = optarg;
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
  *mountpath = argv[1];
  return 0;
}

bool should_use_admin_protocol(fs_management::DiskFormat df) {
  // Newer filesystems (esp. written in rust) don't support the admin protocol, so we won't open it
  // with O_ADMIN.
  switch (df) {
    case fs_management::kDiskFormatFxfs:
    case fs_management::kDiskFormatFat:
      return false;
    default:
      return true;
  }
}

int main(int argc, char** argv) {
  char* devicepath;
  char* mountpath;
  fs_management::MountOptions options;
  if (int r = parse_args(argc, argv, &options, &devicepath, &mountpath)) {
    return r;
  }

  if (options.verbose_mount) {
    printf("fs_mount: Mounting device [%s] on path [%s]\n", devicepath, mountpath);
  }

  fbl::unique_fd fd(open(devicepath, O_RDWR));
  if (!fd) {
    fprintf(stderr, "Error opening block device\n");
    return EXIT_FAILURE;
  }
  fs_management::DiskFormat df = fs_management::DetectDiskFormat(fd.get());
  options.admin = should_use_admin_protocol(df);
  auto result = fs_management::Mount(std::move(fd), mountpath, df, options, launch_logs_async);
  if (result.is_error()) {
    fprintf(stderr, "fs_mount: Error while mounting: %s\n", result.status_string());
  }
  return EXIT_FAILURE;
}
