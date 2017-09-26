// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_
#define GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_

#include "magma_system_connection.h"
#include "msd.h"
#include "platform_connection.h"
#include "platform_event.h"
#include "platform_thread.h"
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
class MagmaSystemSemaphore;

class MagmaSystemDevice {
public:
    static std::unique_ptr<MagmaSystemDevice> Create(msd_device_unique_ptr_t msd_device)
    {
        msd_connection_t* connection =
            msd_device_open(msd_device.get(), magma::PlatformThreadId().id());
        if (!connection)
            return DRETP(nullptr, "couldn't open connection");

        return std::make_unique<MagmaSystemDevice>(std::move(msd_device),
                                                   MsdConnectionUniquePtr(connection));
    }

    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev, msd_connection_unique_ptr_t msd_connection)
        : msd_dev_(std::move(msd_dev)), msd_connection_(std::move(msd_connection))
    {
        connection_map_ = std::make_unique<std::unordered_map<std::thread::id, Connection>>();
    }

    // Opens a connection to the device. On success |connection_handle_out| will contain the
    // connection handle to be passed to the client
    static std::shared_ptr<magma::PlatformConnection>
    Open(std::shared_ptr<MagmaSystemDevice>, msd_client_id_t client_id, uint32_t capabilities);

    msd_device_t* msd_dev() { return msd_dev_.get(); }

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

    void PageFlip(MagmaSystemConnection* connection, std::shared_ptr<MagmaSystemBuffer> buf,
                  magma_system_image_descriptor* image_desc, uint32_t wait_semaphore_count,
                  uint32_t signal_semaphore_count,
                  std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores,
                  std::unique_ptr<magma::PlatformSemaphore> buffer_presented_semaphore);

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
    msd_connection_unique_ptr_t msd_connection_; // for presenting buffers

    std::unordered_map<uint64_t, std::weak_ptr<MagmaSystemBuffer>> buffer_map_;
    std::mutex buffer_map_mutex_;

    bool page_flip_enable_ = true;
    std::mutex page_flip_mutex_;

    struct DeferredFlip {
        std::vector<std::shared_ptr<MagmaSystemSemaphore>> wait;
        std::vector<std::shared_ptr<MagmaSystemSemaphore>> signal;
    };
    std::vector<DeferredFlip> deferred_flip_semaphores_;
    std::vector<uint64_t> deferred_flip_buffers_;
    std::shared_ptr<MagmaSystemBuffer> last_flipped_buffer_;

    struct Connection {
        std::thread thread;
        std::shared_ptr<magma::PlatformEvent> shutdown_event;
    };

    std::unique_ptr<std::unordered_map<std::thread::id, Connection>> connection_map_;
    std::mutex connection_list_mutex_;
};

#endif  // GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_