// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_buffer_manager.h"
#include "magma_util/macros.h"

MagmaSystemBufferManager::MagmaSystemBufferManager(Owner* owner) : owner_(owner)
{
    DASSERT(owner_);
}

bool MagmaSystemBufferManager::ImportBuffer(uint32_t handle, uint64_t* id_out)
{
    auto buf = owner_->GetBufferForHandle(handle);
    if (!buf)
        return DRETF(false, "failed to get buffer for handle");

    uint64_t id = buf->id();

    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        buffer_map_.insert(std::make_pair(id, buf));

    *id_out = id;
    return true;
}

bool MagmaSystemBufferManager::ReleaseBuffer(uint64_t id)
{
    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETF(false, "Attempting to free invalid buffer id");

    buffer_map_.erase(iter);
    // Now that our shared reference has been dropped we tell our
    // owner that were done with the buffer
    owner_->ReleaseBuffer(id);

    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemBufferManager::LookupBuffer(uint64_t id)
{
    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETP(nullptr, "Attempting to lookup invalid buffer id");

    return iter->second;
}