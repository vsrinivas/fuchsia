// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(msd_dev()); }

bool MagmaSystemDevice::Open(msd_client_id client_id, uint32_t* connection_handle_out)
{
    msd_connection* msd_connection = msd_device_open(msd_dev(), client_id);
    if (!msd_connection)
        return DRETF(false, "msd_device_open failed");

    auto iter = connection_map_.find(client_id);
    if (iter != connection_map_.end())
        return DRETF(false, "connection already open for client_id 0x%lx", client_id);

    auto connection = magma::PlatformConnection::Create(std::unique_ptr<MagmaSystemConnection>(
        new MagmaSystemConnection(this, MsdConnectionUniquePtr(msd_connection))));

    *connection_handle_out = connection->GetHandle();
    connection_map_.insert(std::make_pair(client_id, std::move(connection)));

    return true;
}

void MagmaSystemDevice::PageFlip(std::shared_ptr<MagmaSystemBuffer> buf,
                                 magma_system_pageflip_callback_t callback, void* data)
{
    msd_device_page_flip(msd_dev(), buf->msd_buf(), callback, data);
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::GetBufferForHandle(uint32_t handle)
{
    uint64_t id;
    if (!magma::PlatformBuffer::IdFromHandle(handle, &id))
        return DRETP(nullptr, "invalid buffer handle");

    auto iter = buffer_map_.find(id);
    if (iter != buffer_map_.end()) {
        auto buf = iter->second.lock();
        if (buf)
            return buf;
    }

    std::shared_ptr<MagmaSystemBuffer> buf =
        MagmaSystemBuffer::Create(magma::PlatformBuffer::Import(handle));
    buffer_map_.insert(std::make_pair(id, buf));
    return buf;
}

void MagmaSystemDevice::ReleaseBuffer(uint64_t id)
{
    auto iter = buffer_map_.find(id);
    if (iter != buffer_map_.end() && iter->second.expired())
        buffer_map_.erase(iter);
}