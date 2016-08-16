// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemConnection::MagmaSystemConnection(MagmaSystemDevice* device,
                                             msd_connection_unique_ptr_t msd_connection)
    : device_(device), msd_connection_(std::move(msd_connection))
{
    DASSERT(device_);
    DASSERT(msd_connection_);
    magic_ = kMagic;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemConnection::AllocateBuffer(uint64_t size)
{
    std::shared_ptr<MagmaSystemBuffer> buffer(MagmaSystemBuffer::Create(size));
    if (!buffer)
        return DRETP(nullptr, "Failed to create MagmaSystemBuffer");

    buffer_map_.insert(std::make_pair(buffer->handle(), buffer));
    return buffer;
}

bool MagmaSystemConnection::FreeBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return DRETF(false, "MagmaSystemConnection: Attempting to free invalid buffer handle");

    buffer_map_.erase(iter);
    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemConnection::LookupBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return DRETP(nullptr, "MagmaSystemConnection: Attempting to lookup invalid buffer handle");

    return iter->second;
}

bool MagmaSystemConnection::CreateContext(uint32_t* context_id_out)
{
    auto ctx = MagmaSystemContext::Create(this);
    if (!ctx)
        return DRETF(false, "MagmaSystemConnection: Failed to Create MagmaSystemContext");

    auto context_id = next_context_id_++;
    DASSERT(context_map_.find(context_id) == context_map_.end());

    context_map_.insert(std::make_pair(context_id, std::move(ctx)));
    *context_id_out = context_id;
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
