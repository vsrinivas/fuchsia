// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/zxio.h>
#include <stdio.h>

// This tests using the zxio library in a standalone C program.
int main(int argc, char** argv) {
  zxio_storage_t storage;
  zx_status_t status = zxio_create(ZX_HANDLE_INVALID, &storage);
  if (status != ZX_ERR_INVALID_ARGS) {
    return 1;
  }
  return 0;
}
