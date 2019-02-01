// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_SEMAPHORE_H
#define MSD_INTEL_SEMAPHORE_H

#include "magma_util/macros.h"
#include "msd_defs.h"
#include "platform_semaphore.h"

class MsdIntelAbiSemaphore : public msd_semaphore_t {
public:
    MsdIntelAbiSemaphore(std::shared_ptr<magma::PlatformSemaphore> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiSemaphore* cast(msd_semaphore_t* semaphore)
    {
        DASSERT(semaphore);
        DASSERT(semaphore->magic_ == kMagic);
        return static_cast<MsdIntelAbiSemaphore*>(semaphore);
    }

    std::shared_ptr<magma::PlatformSemaphore> ptr() { return ptr_; }

private:
    std::shared_ptr<magma::PlatformSemaphore> ptr_;

    static constexpr uint32_t kMagic = 0x73656d61; // "sema"
};

#endif // MSD_INTEL_SEMAPHORE_H
