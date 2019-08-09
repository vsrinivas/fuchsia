// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <fs-test-utils/blobfs/bloblist.h>
#include <zircon/assert.h>

#include <stdio.h>

namespace {

constexpr char kUsage[] = R"(
Usage:

    %s <seed> <mount-point> <num-ops>
    Performs random operations on a blobfs partition until killed. The blobfs partition should be
    mounted at the provided mount point, and already formatted. The operations will be additive
    (it won't delete files that are already there).

    [Required Arguments]
        seed                An unsigned integer to initialize pseudo-random number generator.

        mount-point         Path to a mounted blobfs partition bound in this program's namespace.
                            Must be mounted read/write.

        num-ops             Number of operations to perform. If 0 is provided, it will perform
                            infinite operations. The combination of a provided seed and num-ops
                            will produce deterministic behavior.
)";

void PrintUsage(const char* arg0) { printf(kUsage, arg0); }

bool ParseCommandLineArgs(int argc, const char* const* argv, unsigned int* seed,
                          fbl::String* mount_point, unsigned int* num_ops) {
  if (argc != 4) {
    printf("missing (or too many?) arguments.\n");
    PrintUsage(argv[0]);
    return false;
  }

  *seed = static_cast<unsigned int>(strtoul(argv[1], NULL, 0));
  *mount_point = fbl::String(argv[2]);
  *num_ops = static_cast<unsigned int>(strtoul(argv[3], NULL, 0));

  return true;
}

bool GenerateLoad(unsigned int seed, fbl::String mount_point, unsigned int num_ops) {
  printf("performing random operations on provided partition...\n");

  fs_test_utils::BlobList blob_list(mount_point);
  unsigned int ops_performed = 0;

  while (num_ops == 0 || ops_performed++ < num_ops) {
    switch (rand_r(&seed) % 6) {
      case 0:
        ZX_ASSERT(blob_list.CreateBlob(&seed));
        break;
      case 1:
        ZX_ASSERT(blob_list.ConfigBlob());
        break;
      case 2:
        // ZX-4689 - it's possible that we will run out of space on this or the truncate. If we
        // do, this errors out.
        ZX_ASSERT(blob_list.WriteData());
        break;
      case 3:
        ZX_ASSERT(blob_list.ReadData());
        break;
      case 4:
        ZX_ASSERT(blob_list.ReopenBlob());
        break;
      case 5:
        ZX_ASSERT(blob_list.UnlinkBlob());
        break;
      default:
        // changed the number of possible operations and didn't add another case branch.
        ZX_ASSERT(false);
        break;
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  unsigned int seed, num_ops;
  fbl::String mount_point;
  if (!ParseCommandLineArgs(argc, argv, &seed, &mount_point, &num_ops)) {
    return -1;
  }

  return GenerateLoad(seed, mount_point, num_ops) ? 0 : -1;
}
