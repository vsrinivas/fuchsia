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
        return std::make_unique<MagmaSystemDevice>(std::move(msd_device));
    }

    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev) : msd_dev_(std::move(msd_dev))
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

    struct Connection {
        std::thread thread;
        std::shared_ptr<magma::PlatformEvent> shutdown_event;
    };

    std::unique_ptr<std::unordered_map<std::thread::id, Connection>> connection_map_;
    std::mutex connection_list_mutex_;
};

#endif  // GARNET_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_
