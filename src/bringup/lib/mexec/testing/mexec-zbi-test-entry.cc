// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include "src/bringup/lib/mexec/mexec.h"
#include "zbi-test-entry.h"

int main(int argc, char** argv) {
  ZbiTestEntry test;
  if (auto result = test.Init(argc, argv); result.is_error()) {
    return result.status_value();
  }

  printf("%s: Booting test ZBI via mexec...\n", argv[0]);

  zx_status_t status = mexec::BootZbi(zx::unowned_resource{test.root_resource()},
                                      std::move(test.kernel_zbi()), std::move(test.data_zbi()));
  ZX_ASSERT(status != ZX_OK);  // If it succeeded, it never returned.
  printf("%s: mexec failed: %s\n", argv[0], zx_status_get_string(status));
  return status;
}
