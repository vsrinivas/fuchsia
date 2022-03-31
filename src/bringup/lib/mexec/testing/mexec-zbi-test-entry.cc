// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <cstdio>

#include "zbi-test-entry.h"

int main(int argc, char** argv) {
  ZbiTestEntry test;
  if (auto result = test.Init(argc, argv); result.is_error()) {
    return result.status_value();
  }

  zx_status_t status = zx_system_mexec(test.root_resource().release(), test.kernel_zbi().release(),
                                       test.data_zbi().release());

  if (status != ZX_OK) {
    printf("%s: zx_system_mexec(): %s\n", argv[0], zx_status_get_string(status));
    return status;
  }

  return 0;
}
