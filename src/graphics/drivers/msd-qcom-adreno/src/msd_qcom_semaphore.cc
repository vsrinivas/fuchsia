// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <msd.h>

#include <magma_util/macros.h>

magma_status_t msd_semaphore_import(uint32_t handle, msd_semaphore_t** semaphore_out) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

void msd_semaphore_release(msd_semaphore_t* semaphore) {
  DMESSAGE("msd_semaphore_release not implemented");
}
