// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(msd_dev()); }

std::unique_ptr<magma::PlatformConnection> MagmaSystemDevice::Open(msd_client_id client_id,
                                                                   uint32_t capabilities)
{
    // at least one bit must be one and it must be one of the 2 least significant bits
    if (!capabilities ||
        (capabilities & ~(MAGMA_SYSTEM_CAPABILITY_DISPLAY | MAGMA_SYSTEM_CAPABILITY_RENDERING)))
        return DRETP(nullptr, "attempting to open connection to device with invalid capabilities");

    msd_connection* msd_connection = msd_device_open(msd_dev(), client_id);
    if (!msd_connection)
        return DRETP(nullptr, "msd_device_open failed");

    return magma::PlatformConnection::Create(std::make_unique<MagmaSystemConnection>(
        this, MsdConnectionUniquePtr(msd_connection), capabilities));
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

    std::unique_lock<std::mutex> lock(buffer_map_mutex_);

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
    std::unique_lock<std::mutex> lock(buffer_map_mutex_);
    auto iter = buffer_map_.find(id);
    if (iter != buffer_map_.end() && iter->second.expired())
        buffer_map_.erase(iter);
}
