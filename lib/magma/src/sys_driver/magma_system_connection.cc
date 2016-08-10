// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemConnection::MagmaSystemConnection(MagmaSystemDevice* device,
                                             msd_connection_unique_ptr_t msd_connection)
    : device_(device), msd_connection_(std::move(msd_connection))
{
    DASSERT(device_);
    DASSERT(msd_connection_);
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
