// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsi_semaphore.h"

magma_status_t msd_semaphore_import(uint32_t handle, uint64_t client_id,
                                    msd_semaphore_t** semaphore_out) {
  auto semaphore = magma::PlatformSemaphore::Import(handle);
  if (!semaphore)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "couldn't import semaphore handle 0x%x", handle);

  semaphore->set_local_id(client_id);

  *semaphore_out =
      new MsdVsiAbiSemaphore(std::shared_ptr<magma::PlatformSemaphore>(std::move(semaphore)));
  return MAGMA_STATUS_OK;
}

void msd_semaphore_release(msd_semaphore_t* semaphore) {
  delete MsdVsiAbiSemaphore::cast(semaphore);
}
