// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugdata.h"

#include <assert.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/sanitizer.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return 1;
  }

  if (!strcmp(argv[1], "publish_data")) {
    zx::vmo vmo;
    ZX_ASSERT(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo) == ZX_OK);
    ZX_ASSERT(vmo.write(kTestData, 0, sizeof(kTestData)) == ZX_OK);
    ZX_ASSERT(vmo.set_property(ZX_PROP_NAME, kTestName, sizeof(kTestName)) == ZX_OK);
    __sanitizer_publish_data(kTestName, vmo.release());
  } else if (!strcmp(argv[1], "load_config")) {
    zx::vmo vmo;
    zx_status_t status = __sanitizer_get_configuration(kTestName, vmo.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    uint8_t config[sizeof(kTestData)];
    ZX_ASSERT(vmo.read(config, 0, sizeof(config)) == ZX_OK);
    ZX_ASSERT(!memcmp(config, kTestData, sizeof(config)));
  } else {
    return 1;
  }

  return 0;
}
