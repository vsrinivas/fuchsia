// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

// Tiny program to exercise ZX_POL_AMBIENT_MARK_VMO_EXEC. Returns zero if
// zx_vmo_replace_as_executable using an invalid resource handle succeeds, one if it fails due to
// ZX_ERR_ACCESS_DENIED, or a negative value if it fails for any other reason.
int main() {
  zx::vmo vmo;
  if (zx::vmo::create(1, 0, &vmo) != ZX_OK) {
    return -1;
  }

  zx_status_t status = vmo.replace_as_executable(zx::resource(), &vmo);
  if (status == ZX_ERR_ACCESS_DENIED) {
    return 1;
  } else if (status != ZX_OK) {
    return -2;
  }
  return 0;
}
