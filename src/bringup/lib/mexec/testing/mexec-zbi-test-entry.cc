// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
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

  // TODO(fxbug.dev/107535): This is a short-term band-aid to address the
  // likely issue of this program mexec'ing before the secondary CPUs have
  // been brought up.
  zx::nanosleep(zx::deadline_after(zx::sec(3)));

  zx_status_t status = mexec::BootZbi(test.mexec_resource(), std::move(test.kernel_zbi()),
                                      std::move(test.data_zbi()));
  ZX_ASSERT(status != ZX_OK);  // If it succeeded, it never returned.
  printf("%s: mexec failed: %s\n", argv[0], zx_status_get_string(status));
  return status;
}
