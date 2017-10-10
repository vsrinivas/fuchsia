// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"
#include "platform_object.h"

uint32_t MagmaSystemDevice::GetDeviceId()
{
    uint64_t result;
    magma::Status status = Query(MAGMA_QUERY_DEVICE_ID, &result);
    DASSERT(status.ok());
    DASSERT(result >> 32 == 0);
    return static_cast<uint32_t>(result);
}

std::shared_ptr<magma::PlatformConnection>
MagmaSystemDevice::Open(std::shared_ptr<MagmaSystemDevice> device, msd_client_id_t client_id,
                        uint32_t capabilities)
{
    // at least one bit must be one and it must be one of the 2 least significant bits
    if (!capabilities || (capabilities & ~(MAGMA_CAPABILITY_DISPLAY | MAGMA_CAPABILITY_RENDERING)))
        return DRETP(nullptr, "attempting to open connection to device with invalid capabilities");

    msd_connection_t* msd_connection = msd_device_open(device->msd_dev(), client_id);
    if (!msd_connection)
        return DRETP(nullptr, "msd_device_open failed");

    return magma::PlatformConnection::Create(std::make_unique<MagmaSystemConnection>(
        std::move(device), MsdConnectionUniquePtr(msd_connection), capabilities));
}

// Called by the driver thread
std::shared_ptr<MagmaSystemBuffer>
MagmaSystemDevice::PageFlipAndEnable(std::shared_ptr<MagmaSystemBuffer> buf,
                                     magma_system_image_descriptor* image_desc, bool enable)
{
    std::unique_lock<std::mutex> lock(page_flip_mutex_);
    std::shared_ptr<MagmaSystemBuffer> last_buffer_copy;

    msd_connection_present_buffer(msd_connection_.get(), buf->msd_buf(), image_desc, 0, 0, nullptr,
                                  nullptr, nullptr);

    DLOG("PageFlipAndEnable enable %d", enable);
    page_flip_enable_ = enable;

    if (enable) {
        for (auto& deferred_semaphores : deferred_flip_semaphores_) {
            for (auto iter : deferred_semaphores.wait) {
                DLOG("waiting for semaphore %lu", iter->platform_semaphore()->id());
                if (!iter->platform_semaphore()->Wait(100))
                    DLOG("timeout waiting for semaphore");
            }
            for (auto iter : deferred_semaphores.signal) {
                DLOG("signalling semaphore %lu", iter->platform_semaphore()->id());
                iter->platform_semaphore()->Signal();
            }
        }
        deferred_flip_buffers_.clear();
        deferred_flip_semaphores_.clear();
        last_flipped_buffer_ = nullptr;

    } else if (last_flipped_buffer_) {
        void* src;
        if (last_flipped_buffer_->platform_buffer()->MapCpu(&src)) {
            std::unique_ptr<magma::PlatformBuffer> copy =
                magma::PlatformBuffer::Create(last_flipped_buffer_->size(), "last_buffer_copy");

            void* dst;
            if (copy->MapCpu(&dst)) {
                memcpy(dst, src, copy->size());
                copy->UnmapCpu();
                last_buffer_copy = MagmaSystemBuffer::Create(std::move(copy));
            }
            last_flipped_buffer_->platform_buffer()->UnmapCpu();
        }
    }

    return last_buffer_copy;
}

static void page_flip_callback(magma_status_t status, uint64_t vblank_time_ns, void* data)
{
    if (status != MAGMA_STATUS_OK) {
        DLOG("page_flip_callback: error status %d", status);
        return;
    }

    auto semaphore = reinterpret_cast<magma::PlatformSemaphore*>(data);
    semaphore->Signal();
    delete semaphore;
}

// Called by display connection threads
void MagmaSystemDevice::PageFlip(
    MagmaSystemConnection* connection, std::shared_ptr<MagmaSystemBuffer> buf,
    magma_system_image_descriptor* image_desc, uint32_t wait_semaphore_count,
    uint32_t signal_semaphore_count, std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores,
    std::unique_ptr<magma::PlatformSemaphore> buffer_presented_semaphore)
{
    std::unique_lock<std::mutex> lock(page_flip_mutex_);

    if (!page_flip_enable_) {
        DeferredFlip deferred_flip;

        for (uint32_t i = 0; i < wait_semaphore_count; i++) {
            DLOG("page flip disabled buffer %lu wait semaphore %lu", buf->platform_buffer()->id(),
                 semaphores[i]->platform_semaphore()->id());
            deferred_flip.wait.push_back(semaphores[i]);
        }
        for (uint32_t i = wait_semaphore_count; i < semaphores.size(); i++) {
            DLOG("page flip disabled buffer %lu signal semaphore %lu", buf->platform_buffer()->id(),
                 semaphores[i]->platform_semaphore()->id());
            deferred_flip.signal.push_back(semaphores[i]);
        }

        auto iter = std::find(std::begin(deferred_flip_buffers_), std::end(deferred_flip_buffers_),
                              buf->platform_buffer()->id());
        if (iter == deferred_flip_buffers_.end()) {
            // This buffer hasn't been flipped so its rendering should complete.
            // Consume the wait semaphores now.
            for (auto iter : deferred_flip.wait) {
                DLOG("waiting for semaphore %lu", iter->platform_semaphore()->id());
                if (!iter->platform_semaphore()->Wait(100))
                    DLOG("timeout waiting for semaphore");
            }
            deferred_flip.wait.clear();
        }

        deferred_flip_buffers_.push_back(buf->platform_buffer()->id());
        deferred_flip_semaphores_.push_back(std::move(deferred_flip));
        return;
    }

    DASSERT(wait_semaphore_count + signal_semaphore_count == semaphores.size());

    std::vector<msd_semaphore_t*> msd_semaphores(semaphores.size());
    for (uint32_t i = 0; i < semaphores.size(); i++) {
        msd_semaphores[i] = semaphores[i]->msd_semaphore();
    }

    msd_connection_present_buffer(connection->msd_connection(), buf->msd_buf(), image_desc,
                                  wait_semaphore_count, signal_semaphore_count,
                                  msd_semaphores.data(), page_flip_callback,
                                  buffer_presented_semaphore.release());

    last_flipped_buffer_ = buf;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::ImportBuffer(uint32_t handle)
{
    auto platform_buf = magma::PlatformBuffer::Import(handle);
    uint64_t id = platform_buf->id();

    std::unique_lock<std::mutex> lock(buffer_map_mutex_);

    auto iter = buffer_map_.find(id);
    if (iter != buffer_map_.end()) {
        auto buf = iter->second.lock();
        if (buf)
            return buf;
    }

    std::shared_ptr<MagmaSystemBuffer> buf = MagmaSystemBuffer::Create(std::move(platform_buf));
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
