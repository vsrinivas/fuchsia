// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemConnection::MagmaSystemConnection(Owner* owner,
                                             msd_connection_unique_ptr_t msd_connection)
    : MagmaSystemBufferManager(owner), owner_(owner), msd_connection_(std::move(msd_connection))
{
    DASSERT(owner_);
    DASSERT(msd_connection_);
}

bool MagmaSystemConnection::CreateContext(uint32_t context_id)
{
    auto iter = context_map_.find(context_id);
    if (iter != context_map_.end())
        return DRETF(false, "Attempting to add context with duplicate id");

    auto msd_ctx = msd_connection_create_context(msd_connection());
    if (!msd_ctx)
        return DRETF(false, "Failed to create msd context");

    auto ctx = std::unique_ptr<MagmaSystemContext>(
        new MagmaSystemContext(this, msd_context_unique_ptr_t(msd_ctx, &msd_context_destroy)));

    context_map_.insert(std::make_pair(context_id, std::move(ctx)));
    return true;
}

bool MagmaSystemConnection::DestroyContext(uint32_t context_id)
{
    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETF(false, "MagmaSystemConnection:Attempting to destroy invalid context id");
    context_map_.erase(iter);
    return true;
}

MagmaSystemContext* MagmaSystemConnection::LookupContext(uint32_t context_id)
{
    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETP(nullptr, "MagmaSystemConnection: Attempting to lookup invalid context id");

    return iter->second.get();
}

bool MagmaSystemConnection::ExecuteCommandBuffer(magma_system_command_buffer* command_buffer,
                                                 uint32_t context_id)
{
    auto context = LookupContext(context_id);
    if (!context)
        return DRETF(false, "Attempting to execute command buffer on invalid context");

    if (!context->ExecuteCommandBuffer(command_buffer))
        return DRETF(false, "Context failed to execute command buffer");

    return true;
}
