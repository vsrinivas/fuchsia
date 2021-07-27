// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>
#include <limits.h>
#include <time.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

bool gUseRamDisk = true;
unsigned int gRandSeed = 1;
char gDevPath[PATH_MAX];

int main(int argc, char** argv) {
  gRandSeed = static_cast<unsigned int>(time(nullptr));
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-d") && (i + 1 < argc)) {
      snprintf(gDevPath, sizeof(gDevPath), "%s", argv[i + 1]);
      gUseRamDisk = false;
    } else if (!strcmp(argv[i], "-s") && (i + 1 < argc)) {
      gRandSeed = static_cast<unsigned int>(strtoul(argv[i + 1], NULL, 0));
    } else {
      // Ignore options we don't recognize. See ulib/unittest/README.md.
      continue;
    }
  }
  fprintf(stdout, "Starting test with %u\n", gRandSeed);
  srand(gRandSeed);

  // isolated_devmgr loads drivers asynchronously, causing an inherent race here. Wait for the
  // ramdisk driver to load before proceeding with the test to ensure it's there when we need it.
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  if (!dev) {
    fprintf(stderr, "open(\"/dev\"): %s\n", strerror(errno));
    return -1;
  }
  fbl::unique_fd out;
  zx_status_t status =
      devmgr_integration_test::RecursiveWaitForFile(dev, "sys/platform/00:00:2d/ramctl", &out);
  if (status != ZX_OK) {
    fprintf(stderr, "RecursiveWaitForFile(dev, \"sys/platform/00:00:2d/ramctl\"): %s\n",
            zx_status_get_string(status));
    return -1;
  }

  return RUN_ALL_TESTS(argc, argv);
}
