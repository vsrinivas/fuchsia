// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"
#include "platform_object.h"

uint32_t MagmaSystemDevice::GetDeviceId()
{
    uint64_t result;
    magma::Status status = Query(MAGMA_QUERY_DEVICE_ID, &result);
    if (!status.ok())
        return 0;
    DASSERT(result >> 32 == 0);
    return static_cast<uint32_t>(result);
}

std::shared_ptr<magma::PlatformConnection>
MagmaSystemDevice::Open(std::shared_ptr<MagmaSystemDevice> device, msd_client_id_t client_id,
                        uint32_t capabilities)
{
    // at least one valid capability must be set, and rendering is the only valid capability
    if (capabilities != MAGMA_CAPABILITY_RENDERING)
        return DRETP(nullptr, "attempting to open connection to device with invalid capabilities");

    msd_connection_t* msd_connection = msd_device_open(device->msd_dev(), client_id);
    if (!msd_connection)
        return DRETP(nullptr, "msd_device_open failed");

    return magma::PlatformConnection::Create(std::make_unique<MagmaSystemConnection>(
        std::move(device), MsdConnectionUniquePtr(msd_connection), capabilities));
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
