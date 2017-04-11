// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DEVICE_H_
#define _MAGMA_SYSTEM_DEVICE_H_

#include "msd.h"
#include "platform_connection.h"
#include "platform_event.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using msd_device_unique_ptr_t = std::unique_ptr<msd_device_t, decltype(&msd_device_destroy)>;

static inline msd_device_unique_ptr_t MsdDeviceUniquePtr(msd_device_t* msd_dev)
{
    return msd_device_unique_ptr_t(msd_dev, &msd_device_destroy);
}

class MagmaSystemBuffer;
class MagmaSystemConnection;
class MagmaSystemSemaphore;

class MagmaSystemDevice {
public:
    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev) : msd_dev_(std::move(msd_dev))
    {
        connection_map_ = std::make_unique<std::unordered_map<std::thread::id, Connection>>();
    }

    // Opens a connection to the device. On success |connection_handle_out| will contain the
    // connection handle to be passed to the client
    static std::shared_ptr<magma::PlatformConnection>
    Open(std::shared_ptr<MagmaSystemDevice>, msd_client_id client_id, uint32_t capabilities);

    msd_device_t* msd_dev() { return msd_dev_.get(); }

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

    void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf, magma_system_image_descriptor* image_desc,
                  uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                  std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores);

    // Returns the last flipped buffer.
    std::shared_ptr<MagmaSystemBuffer> PageFlipAndEnable(std::shared_ptr<MagmaSystemBuffer> buf,
                                                         magma_system_image_descriptor* image_desc,
                                                         bool enable);

    bool page_flip_enabled() { return page_flip_enable_; }

    // Takes ownership of handle and either wraps it up in new MagmaSystemBuffer or
    // closes it and returns an existing MagmaSystemBuffer backed by the same memory
    std::shared_ptr<MagmaSystemBuffer> ImportBuffer(uint32_t handle);
    void ReleaseBuffer(uint64_t id);

    // Called on driver thread
    void Shutdown();

    // Called on driver thread
    void StartConnectionThread(std::shared_ptr<magma::PlatformConnection> platform_connection);

    // Called on connection thread
    void ConnectionClosed(std::thread::id thread_id);

    void DumpStatus() { msd_device_dump_status(msd_dev()); }

    magma::Status Query(uint32_t id, uint64_t* value_out)
    {
        return msd_device_query(msd_dev(), id, value_out);
    }

private:
    msd_device_unique_ptr_t msd_dev_;
    std::unordered_map<uint64_t, std::weak_ptr<MagmaSystemBuffer>> buffer_map_;
    std::mutex buffer_map_mutex_;

    bool page_flip_enable_ = true;
    std::mutex page_flip_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<MagmaSystemSemaphore>>
        flip_deferred_wait_semaphores_;
    std::unordered_map<uint64_t, std::shared_ptr<MagmaSystemSemaphore>>
        flip_deferred_signal_semaphores_;
    std::shared_ptr<MagmaSystemBuffer> last_flipped_buffer_;

    struct Connection {
        std::thread thread;
        std::shared_ptr<magma::PlatformEvent> shutdown_event;
    };

    std::unique_ptr<std::unordered_map<std::thread::id, Connection>> connection_map_;
    std::mutex connection_list_mutex_;
};

#endif //_MAGMA_SYSTEM_DEVICE_H_