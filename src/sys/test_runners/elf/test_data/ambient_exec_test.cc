// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

int main() {
  zx::vmo vmo;
  zx_status_t res;

  res = zx::vmo::create(4096, 0, &vmo);
  if (res != ZX_OK) {
    return 1;
  }

  res = vmo.replace_as_executable(zx::resource(), &vmo);
  if (res != ZX_OK) {
    return 1;
  }

  return 0;
}
