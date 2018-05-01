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

    if (device_id_ != 0x7000)
        return DRETF(false, "Unspported gpu model 0x%x\n", device_id_);

    gpu_features_ = std::make_unique<GpuFeatures>(register_io_.get());
    DLOG("gpu features: 0x%x minor features 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
         gpu_features_->features().reg_value(), gpu_features_->minor_features(0),
         gpu_features_->minor_features(1), gpu_features_->minor_features(2),
         gpu_features_->minor_features(3), gpu_features_->minor_features(4),
         gpu_features_->minor_features(5));
    DLOG("stream count %u register_max %u thread_count %u vertex_cache_size %u shader_core_count "
         "%u pixel_pipes %u vertex_output_buffer_size %u\n",
         gpu_features_->stream_count(), gpu_features_->register_max(),
         gpu_features_->thread_count(), gpu_features_->vertex_cache_size(),
         gpu_features_->shader_core_count(), gpu_features_->pixel_pipes(),
         gpu_features_->vertex_output_buffer_size());
    DLOG("instruction count %u buffer_size %u num_constants %u varyings_count %u\n",
         gpu_features_->instruction_count(), gpu_features_->buffer_size(),
         gpu_features_->num_constants(), gpu_features_->varyings_count());

    if (!gpu_features_->features().pipe_3d().get())
        return DRETF(false, "Gpu has no 3d pipe: features 0x%x\n",
                     gpu_features_->features().reg_value());

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
