// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_CONTEXT_H_
#define _MAGMA_SYSTEM_CONTEXT_H_

#include <functional>
#include <memory>

#include "magma_system_buffer.h"
#include "magma_util/status.h"
#include "msd.h"

using msd_context_unique_ptr_t = std::unique_ptr<msd_context, decltype(&msd_context_destroy)>;

static inline msd_context_unique_ptr_t MsdContextUniquePtr(msd_context* context)
{
    return msd_context_unique_ptr_t(context, &msd_context_destroy);
}

class MagmaSystemContext {
public:
    class Owner {
    public:
        virtual std::shared_ptr<MagmaSystemBuffer> LookupBufferForContext(uint64_t id) = 0;
        virtual msd_semaphore* LookupSemaphoreForContext(uint64_t id) = 0;
    };

    MagmaSystemContext(Owner* owner, msd_context_unique_ptr_t msd_ctx)
        : owner_(owner), msd_ctx_(std::move(msd_ctx))
    {
    }

    magma::Status ExecuteCommandBuffer(std::shared_ptr<MagmaSystemBuffer> command_buffer);

private:
    msd_context* msd_ctx() { return msd_ctx_.get(); }

    Owner* owner_;

    msd_context_unique_ptr_t msd_ctx_;

    friend class CommandBufferHelper;
};

#endif // _MAGMA_SYSTEM_CONTEXT_H_