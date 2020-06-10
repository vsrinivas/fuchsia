// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>

#include <cstring>

// Tiny program to exercise ZX_POL_NEW_PROCESS. Returns zero if calling zx_process_create succeeds,
// one if it fails due to ZX_ERR_ACCESS_DENIED, or a negative value if it fails for any other
// reason.
int main() {
  zx::process proc;
  zx::vmar root_vmar;
  const char name[] = "foo";
  zx_status_t status =
      zx::process::create(*zx::job::default_job(), name, strlen(name), 0, &proc, &root_vmar);
  if (status == ZX_ERR_ACCESS_DENIED) {
    return 1;
  }
  if (status != ZX_OK) {
    return -1;
  }
  return 0;
}
