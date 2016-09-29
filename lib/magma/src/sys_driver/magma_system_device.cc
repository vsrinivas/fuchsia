// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(msd_dev()); }

std::unique_ptr<MagmaSystemConnection> MagmaSystemDevice::Open(msd_client_id client_id)
{
    msd_connection* connection = msd_device_open(msd_dev(), client_id);
    if (!connection)
        return DRETP(nullptr, "msd_device_open failed");

    return std::unique_ptr<MagmaSystemConnection>(
        new MagmaSystemConnection(this, MsdConnectionUniquePtr(connection)));
}

void MagmaSystemDevice::PageFlip(std::shared_ptr<MagmaSystemBuffer> buf,
                                 magma_system_pageflip_callback_t callback, void* data)
{
    msd_device_page_flip(msd_dev(), buf->msd_buf(), callback, data);
}

uint32_t MagmaSystemDevice::GetTokenForBuffer(std::shared_ptr<MagmaSystemBuffer> buffer)
{
    uint32_t token = next_token_++;
    token_map_.insert(std::make_pair(token, buffer));
    return token;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::GetBufferForToken(uint32_t token)
{
    auto iter = token_map_.find(token);
    if (iter == token_map_.end())
        return DRETP(nullptr, "Attempting to import invalid buffer token");

    auto buf = iter->second;

    // Should only be able to import once per export
    token_map_.erase(iter);

    return buf;
}