// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_device.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "platform_mmio.h"
#include "registers.h"

std::unique_ptr<MsdVslDevice> MsdVslDevice::Create(void* device_handle)
{
    auto device = std::make_unique<MsdVslDevice>();

    if (!device->Init(device_handle))
        return DRETP(nullptr, "Failed to initialize device");

    return device;
}

bool MsdVslDevice::Init(void* device_handle)
{
    platform_device_ = magma::PlatformDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "Failed to create platform device");

    std::unique_ptr<magma::PlatformMmio> mmio =
        platform_device_->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    if (!mmio)
        return DRETF(false, "failed to map registers");

    register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

    device_id_ = registers::ChipId::Get().ReadFrom(register_io_.get()).chip_id().get();
    DLOG("Detected vsl chip id 0x%x", device_id_);

    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    return DRETP(nullptr, "not implemented");
}

void msd_device_destroy(msd_device_t* dev) {}

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_device_dump_status(msd_device_t* device) {}

magma_status_t msd_device_display_get_size(msd_device_t* dev, magma_display_size* size_out)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}
