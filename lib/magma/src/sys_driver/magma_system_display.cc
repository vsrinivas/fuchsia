// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_display.h"

MagmaSystemDisplay::MagmaSystemDisplay(Owner* owner) : owner_(owner)
{
    DASSERT(owner_);
    magic_ = kMagic;
}

bool MagmaSystemDisplay::ImportBuffer(uint32_t token, uint32_t* handle_out)
{
    auto buffer = owner_->GetBufferForToken(token);
    if (!buffer)
        return DRETF(false, "Attempting to import invalid token");

    auto iter = buffer_map_.find(buffer->handle());
    if (iter != buffer_map_.end())
        return DRETF(false, "Attempting to import duplicate buffer");

    buffer_map_.insert(std::make_pair(buffer->handle(), buffer));

    *handle_out = buffer->handle();

    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDisplay::LookupBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return DRETP(nullptr, "Attempting to lookup invalid buffer handle");

    return iter->second;
}

bool MagmaSystemDisplay::ReleaseBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return DRETF(false, "Attempting to release invalid buffer handle");

    buffer_map_.erase(iter);
    return true;
}