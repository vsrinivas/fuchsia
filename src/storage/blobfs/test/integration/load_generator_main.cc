// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>

#include <string>

#include "load_generator.h"

namespace {

constexpr char kUsage[] = R"(
Usage:
    %s <seed> <mount-point> <num-ops>

    Performs random operations on a blobfs partition until killed. The blobfs
    partition should be mounted at the provided mount point, and already
    formatted. The operations will be additive (it won't delete files that are
    already there).

    [Required Arguments]
        seed          An unsigned integer to initialize pseudo-random number
                      generator.

        mount-point   Path to a mounted blobfs partition bound in this program's
                      namespace.
                      Must be mounted read/write.

        num-ops       Number of operations to perform. If 0 is provided, it will
                      perform infinite operations. The combination of a provided
                      seed and num-ops will produce deterministic behavior.
)";

void PrintUsage(const char* arg0) { printf(kUsage, arg0); }

bool ParseCommandLineArgs(int argc, const char* const* argv, unsigned int* seed,
                          std::string* mount_point, unsigned int* num_ops) {
  if (argc != 4) {
    printf("missing (or too many?) arguments.\n");
    PrintUsage(argv[0]);
    return false;
  }

  *seed = static_cast<unsigned int>(strtoul(argv[1], NULL, 0));
  *mount_point = std::string(argv[2]);
  *num_ops = static_cast<unsigned int>(strtoul(argv[3], NULL, 0));

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  unsigned int seed, num_ops;
  std::string mount_point;
  if (!ParseCommandLineArgs(argc, argv, &seed, &mount_point, &num_ops)) {
    return -1;
  }
  printf("performing random operations on provided file system...\n");

  BlobList blob_list(mount_point.c_str());
  if (num_ops) {
    blob_list.GenerateLoad(num_ops, &seed);
  } else {
    for (;;) {
      blob_list.GenerateLoad(UINT_MAX, &seed);
    }
  }
  return 0;
}
