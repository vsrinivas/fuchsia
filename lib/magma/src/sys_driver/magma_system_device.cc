// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"
#include "platform_object.h"

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(msd_dev()); }

std::shared_ptr<magma::PlatformConnection>
MagmaSystemDevice::Open(std::shared_ptr<MagmaSystemDevice> device, msd_client_id client_id,
                        uint32_t capabilities)
{
    // at least one bit must be one and it must be one of the 2 least significant bits
    if (!capabilities ||
        (capabilities & ~(MAGMA_SYSTEM_CAPABILITY_DISPLAY | MAGMA_SYSTEM_CAPABILITY_RENDERING)))
        return DRETP(nullptr, "attempting to open connection to device with invalid capabilities");

    msd_connection_t* msd_connection = msd_device_open(device->msd_dev(), client_id);
    if (!msd_connection)
        return DRETP(nullptr, "msd_device_open failed");

    return magma::PlatformConnection::Create(std::make_unique<MagmaSystemConnection>(
        std::move(device), MsdConnectionUniquePtr(msd_connection), capabilities));
}

void MagmaSystemDevice::PageFlip(std::shared_ptr<MagmaSystemBuffer> buf,
                                 magma_system_image_descriptor* image_desc,
                                 uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                 std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores)
{
    DASSERT(wait_semaphore_count + signal_semaphore_count == semaphores.size());

    std::vector<msd_semaphore_t*> msd_semaphores(semaphores.size());
    for (uint32_t i = 0; i < semaphores.size(); i++) {
        msd_semaphores[i] = semaphores[i]->msd_semaphore();
    }

    msd_device_page_flip(msd_dev(), buf->msd_buf(), image_desc, wait_semaphore_count,
                         signal_semaphore_count, msd_semaphores.data());
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::GetBufferForHandle(uint32_t handle)
{
    uint64_t id;
    if (!magma::PlatformObject::IdFromHandle(handle, &id))
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

void MagmaSystemDevice::StartConnectionThread(
    std::shared_ptr<magma::PlatformConnection> platform_connection)
{
    std::unique_lock<std::mutex> lock(connection_list_mutex_);

    auto shutdown_event = platform_connection->ShutdownEvent();
    std::thread thread(magma::PlatformConnection::RunLoop, std::move(platform_connection));

    connection_map_->insert(std::pair<std::thread::id, Connection>(
        thread.get_id(), Connection{std::move(thread), std::move(shutdown_event)}));
}

void MagmaSystemDevice::ConnectionClosed(std::thread::id thread_id)
{
    std::unique_lock<std::mutex> lock(connection_list_mutex_);

    if (!connection_map_)
        return;

    auto iter = connection_map_->find(thread_id);
    // May not be in the map if no connection thread was started.
    if (iter != connection_map_->end()) {
        iter->second.thread.detach();
        connection_map_->erase(iter);
    }
}

void MagmaSystemDevice::Shutdown()
{
    std::unique_lock<std::mutex> lock(connection_list_mutex_);
    auto map = std::move(connection_map_);
    lock.unlock();

    for (auto& element : *map) {
        element.second.shutdown_event->Signal();
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (auto& element : *map) {
        element.second.thread.join();
    }

    std::chrono::duration<double, std::milli> elapsed =
        std::chrono::high_resolution_clock::now() - start;
    DLOG("shutdown took %u ms", (uint32_t)elapsed.count());

    (void)elapsed;
}
