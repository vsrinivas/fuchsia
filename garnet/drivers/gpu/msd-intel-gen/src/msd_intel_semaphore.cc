// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_semaphore.h"
#include "magma_util/macros.h"
#include "msd.h"

magma_status_t msd_semaphore_import(uint32_t handle, msd_semaphore_t** semaphore_out)
{
    auto semaphore = magma::PlatformSemaphore::Import(handle);
    if (!semaphore)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "couldn't import semaphore handle");

    *semaphore_out =
        new MsdIntelAbiSemaphore(std::shared_ptr<magma::PlatformSemaphore>(std::move(semaphore)));

    return MAGMA_STATUS_OK;
}

void msd_semaphore_release(msd_semaphore_t* semaphore)
{
    delete MsdIntelAbiSemaphore::cast(semaphore);
}
