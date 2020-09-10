// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/namespace.h>
#include <stdio.h>
#include <zircon/status.h>

// This is a test executable to examine what happens when a process is not
// given any namespace entries.
int main(int argc, char** argv) {
  fdio_ns_t* ns = NULL;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    printf("fdio_ns_get_installed returned: %d (%s)\n", status, zx_status_get_string(status));
    return 1;
  }

  if (ns == NULL) {
    printf("global ns was null\n");
    return 1;
  }

  fdio_flat_namespace_t* flat = NULL;
  status = fdio_ns_export_root(&flat);
  if (status != ZX_OK) {
    printf("fdio_ns_export_root returned: %d (%s)\n", status, zx_status_get_string(status));
    return 1;
  }

  if (flat == NULL) {
    printf("exported flat namespace was null\n");
    return 1;
  }

  if (flat->count != 0) {
    printf("exported flat namespace was non-empty\n");
    return 1;
  }

  fdio_ns_free_flat_ns(flat);
  return 0;
}
