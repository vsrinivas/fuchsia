// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DEVICE_H_
#define _MAGMA_SYSTEM_DEVICE_H_

#include "msd.h"
#include "platform_connection.h"
#include "platform_event.h"
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

using msd_device_unique_ptr_t = std::unique_ptr<msd_device, decltype(&msd_device_destroy)>;

static inline msd_device_unique_ptr_t MsdDeviceUniquePtr(msd_device* msd_dev)
{
    return msd_device_unique_ptr_t(msd_dev, &msd_device_destroy);
}

class MagmaSystemBuffer;

class MagmaSystemDevice {
public:
    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev) : msd_dev_(std::move(msd_dev)) {}

    // Opens a connection to the device. On success |connection_handle_out| will contain the
    // connection handle to be passed to the client
    static std::unique_ptr<magma::PlatformConnection>
    Open(std::shared_ptr<MagmaSystemDevice>, msd_client_id client_id, uint32_t capabilities);

    msd_device* msd_dev() { return msd_dev_.get(); }

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

    void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf, magma_system_pageflip_callback_t callback,
                  void* data);

    std::shared_ptr<MagmaSystemBuffer> GetBufferForHandle(uint32_t handle);
    void ReleaseBuffer(uint64_t id);

    bool Shutdown(uint32_t timeout_ms);

    void ConnectionOpened(std::shared_ptr<magma::PlatformEvent> shutdown_event)
    {
        std::unique_lock<std::mutex> lock(connection_list_mutex_);
        connection_list_.push_back(shutdown_event);
    }

    void ConnectionClosed(std::shared_ptr<magma::PlatformEvent> shutdown_event)
    {
        std::unique_lock<std::mutex> lock(connection_list_mutex_);
        connection_list_.remove(shutdown_event);
    }

private:
    msd_device_unique_ptr_t msd_dev_;
    std::unordered_map<uint64_t, std::weak_ptr<MagmaSystemBuffer>> buffer_map_;
    std::mutex buffer_map_mutex_;

    std::list<std::shared_ptr<magma::PlatformEvent>> connection_list_;
    std::mutex connection_list_mutex_;
};

#endif //_MAGMA_SYSTEM_DEVICE_H_