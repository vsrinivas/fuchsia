// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <getopt.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <string>

#include "src/storage/fshost/constants.h"

bool verbose = false;

#define xprintf(fmt...) \
  do {                  \
    if (verbose)        \
      printf(fmt);      \
  } while (0)

int usage(void) {
  fprintf(stderr,
          "usage: umount [ <option>* ] <mount-name>\n"
          "options:\n"
          " -v|--verbose   : Verbose mode\n"
          " --fshost-path  : The path to the fshost admin service (if different from the default)\n"
          " -h|--help      : Display this message\n");
  return EXIT_FAILURE;
}

int main(int argc, char** argv) {
  std::string fshost_path(fshost::kHubAdminServicePath);
  while (1) {
    static struct option opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"verbose", no_argument, nullptr, 'v'},
        {"fshost-path", required_argument, nullptr, 'p'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "v", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'v':
        verbose = true;
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

  if (argc < 1) {
    return usage();
  }
  std::filesystem::path path(argv[0]);
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

  std::string mount_name = path.filename();
  xprintf("Unmount path: /mnt/%s\n", mount_name.c_str());

  auto client_or = component::Connect<fuchsia_fshost::Admin>(fshost_path.c_str());
  if (client_or.is_error()) {
    fprintf(stderr, "Error connecting to fshost (@ %s): %s\n", fshost_path.c_str(),
            client_or.status_string());
    return EXIT_FAILURE;
  }

  auto result = fidl::WireCall(*client_or)->Unmount(fidl::StringView::FromExternal(mount_name));
  if (!result.ok()) {
    fprintf(stderr, "Error unmounting, fidl error: %s\n", result.FormatDescription().c_str());
    return EXIT_FAILURE;
  }
  if (result->is_error()) {
    fprintf(stderr, "Error unmounting: %s\n", zx_status_get_string(result->error_value()));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
