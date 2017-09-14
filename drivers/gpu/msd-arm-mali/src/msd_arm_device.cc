// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_device.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <bitset>
#include <cstdio>
#include <ddk/debug.h>
#include <string>

// This is the index into the mmio section of the mdi.
enum MMIO_INDEX {
    MMIO_INDEX_REGISTERS = 0,
};

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdArmDevice> MsdArmDevice::Create(void* device_handle, bool start_device_thread)
{
    auto device = std::make_unique<MsdArmDevice>();

    if (!device->Init(device_handle))
        return DRETP(nullptr, "Failed to initialize MsdArmDevice");

    if (start_device_thread)
        device->StartDeviceThread();

    return device;
}

MsdArmDevice::MsdArmDevice() { magic_ = kMagic; }

MsdArmDevice::~MsdArmDevice() { Destroy(); }

void MsdArmDevice::Destroy()
{
    DLOG("Destroy");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    device_thread_quit_flag_ = true;

    if (device_request_semaphore_)
        device_request_semaphore_->Signal();

    if (device_thread_.joinable()) {
        DLOG("joining device thread");
        device_thread_.join();
        DLOG("joined");
    }
}

bool MsdArmDevice::Init(void* device_handle)
{
    DLOG("Init");
    platform_device_ = magma::PlatformDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "Failed to initialize device");

    std::unique_ptr<magma::PlatformMmio> mmio = platform_device_->CpuMapMmio(
        MMIO_INDEX_REGISTERS, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    if (!mmio)
        return DRETF(false, "failed to map registers");

    register_io_ = std::make_unique<RegisterIo>(std::move(mmio));

    device_request_semaphore_ = magma::PlatformSemaphore::Create();

    return true;
}

std::unique_ptr<MsdArmConnection> MsdArmDevice::Open(msd_client_id_t client_id)
{
    return MsdArmConnection::Create(client_id);
}

int MsdArmDevice::DeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

    device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

    std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);

    while (true) {
        device_request_semaphore_->Wait();

        if (device_thread_quit_flag_)
            break;
    }

    DLOG("DeviceThreadLoop exit");
    return 0;
}

void MsdArmDevice::StartDeviceThread()
{
    DASSERT(!device_thread_.joinable());
    device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    auto connection = MsdArmDevice::cast(dev)->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "MsdArmDevice::Open failed");
    return new MsdArmAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdArmDevice::cast(dev); }

uint32_t msd_device_get_id(msd_device_t* dev) { return 0; }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

void msd_device_dump_status(msd_device_t* device) {}

magma_status_t msd_device_display_get_size(msd_device_t* dev, magma_display_size* size_out)
{
    return MAGMA_STATUS_OK;
}
