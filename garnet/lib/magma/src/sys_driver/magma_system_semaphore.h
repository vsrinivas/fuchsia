// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_SEMAPHORE_H_
#define GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_SEMAPHORE_H_

#include "msd.h"
#include "platform_semaphore.h"
#include <memory>

using msd_semaphore_unique_ptr_t =
    std::unique_ptr<msd_semaphore_t, decltype(&msd_semaphore_release)>;

static inline msd_semaphore_unique_ptr_t MsdSemaphoreUniquePtr(msd_semaphore_t* semaphore)
{
    return msd_semaphore_unique_ptr_t(semaphore, msd_semaphore_release);
}

class MagmaSystemSemaphore {
public:
    static std::unique_ptr<MagmaSystemSemaphore>
    Create(std::unique_ptr<magma::PlatformSemaphore> platform_semaphore);

    magma::PlatformSemaphore* platform_semaphore() { return platform_semaphore_.get(); }

    msd_semaphore_t* msd_semaphore() { return msd_semaphore_.get(); }

private:
    MagmaSystemSemaphore(std::unique_ptr<magma::PlatformSemaphore> platform_semaphore,
                         msd_semaphore_unique_ptr_t msd_semaphore_t);
    std::unique_ptr<magma::PlatformSemaphore> platform_semaphore_;
    msd_semaphore_unique_ptr_t msd_semaphore_;
};

#endif // GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_SEMAPHORE_H_
