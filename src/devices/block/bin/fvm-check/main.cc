// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>

#include <utility>

#include <fbl/unique_fd.h>

#include "src/storage/fvm/fvm_check.h"

namespace {

constexpr char kUsageMessage[] = R"""(
Validate the metadata of a FVM using a saved image file (or block device).

fvm-check [options] image_file

Options:
  --block-size (-b) xxx : Number of bytes per block. Defaults to 512.
  --silent (-s): Silences all stdout logging info. Defaults to false.
)""";

bool GetOptions(int argc, char** argv, fvm::Checker* checker) {
  while (true) {
    struct option options[] = {
        {"block-size", required_argument, nullptr, 'b'},
        {"silent", no_argument, nullptr, 's'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "b:sh", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'b':
        checker->SetBlockSize(static_cast<uint32_t>(strtoul(optarg, NULL, 0)));
        break;
      case 's':
        checker->SetSilent(true);
        break;
      case 'h':
        return false;
    }
  }
  if (argc == optind + 1) {
    const char* path = argv[optind];
    fbl::unique_fd fd(open(path, O_RDONLY));
    if (!fd) {
      fprintf(stderr, "Cannot open %s\n", path);
      return false;
    }

    checker->SetDevice(std::move(fd));
    return true;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  fvm::Checker checker;
  if (!GetOptions(argc, argv, &checker)) {
    fprintf(stderr, "%s\n", kUsageMessage);
    return -1;
  }

  if (!checker.Validate()) {
    return -1;
  }
  return 0;
}
