// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_device.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "platform_mmio.h"
#include "registers.h"
#include <chrono>
#include <thread>

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
    DLOG("halti5: %d mmu: %d", gpu_features_->halti5(), gpu_features_->has_mmu());

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

    bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
    if (!bus_mapper_)
        return DRETF(false, "failed to create bus mapper");

    page_table_arrays_ = PageTableArrays::Create(bus_mapper_.get());
    if (!page_table_arrays_)
        return DRETF(false, "failed to create page table arrays");

    Reset();
    HardwareInit();

    return true;
}

void MsdVslDevice::HardwareInit()
{
    {
        auto reg = registers::SecureAhbControl::Get().ReadFrom(register_io_.get());
        reg.non_secure_access().set(1);
        reg.WriteTo(register_io_.get());
    }

    page_table_arrays_->HardwareInit(register_io_.get());
}

void MsdVslDevice::Reset()
{
    DLOG("Reset start");

    auto clock_control = registers::ClockControl::Get().FromValue(0);
    clock_control.isolate_gpu().set(1);
    clock_control.WriteTo(register_io());

    {
        auto reg = registers::SecureAhbControl::Get().FromValue(0);
        reg.reset().set(1);
        reg.WriteTo(register_io_.get());
    }

    std::this_thread::sleep_for(std::chrono::microseconds(100));

    clock_control.soft_reset().set(0);
    clock_control.WriteTo(register_io());

    clock_control.isolate_gpu().set(0);
    clock_control.WriteTo(register_io());

    clock_control = registers::ClockControl::Get().ReadFrom(register_io_.get());

    if (!IsIdle() || !clock_control.idle_3d().get()) {
        magma::log(magma::LOG_WARNING, "Gpu reset: failed to idle");
    }

    DLOG("Reset complete");
}

bool MsdVslDevice::IsIdle()
{
    return registers::IdleState::Get().ReadFrom(register_io_.get()).IsIdle();
}

bool MsdVslDevice::SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
                                            uint16_t* prefetch_out)
{
    if (bus_addr & 0xFFFFFFFF00000000ul)
        return DRETF(false, "Can't submit address > 32 bits without mmu: 0x%08lx", bus_addr);

    uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
    if (prefetch & 0xFFFF0000)
        return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

    *prefetch_out = prefetch & 0xFFFF;

    DLOG("Submitting buffer at bus addr 0x%lx", bus_addr);

    auto reg_cmd_addr = registers::FetchEngineCommandAddress::Get().FromValue(0);
    reg_cmd_addr.addr().set(bus_addr & 0xFFFFFFFF);

    auto reg_cmd_ctrl = registers::FetchEngineCommandControl::Get().FromValue(0);
    reg_cmd_ctrl.enable().set(1);
    reg_cmd_ctrl.prefetch().set(*prefetch_out);

    auto reg_sec_cmd_ctrl = registers::SecureCommandControl::Get().FromValue(0);
    reg_sec_cmd_ctrl.enable().set(1);
    reg_sec_cmd_ctrl.prefetch().set(*prefetch_out);

    reg_cmd_addr.WriteTo(register_io_.get());
    reg_cmd_ctrl.WriteTo(register_io_.get());
    reg_sec_cmd_ctrl.WriteTo(register_io_.get());

    return true;
}

bool MsdVslDevice::SubmitCommandBuffer(uint32_t gpu_addr, uint32_t length, uint16_t* prefetch_out)
{
    uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
    if (prefetch & 0xFFFF0000)
        return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

    *prefetch_out = prefetch & 0xFFFF;

    DLOG("Submitting buffer at gpu addr 0x%x", gpu_addr);

    auto reg_cmd_addr = registers::FetchEngineCommandAddress::Get().FromValue(0);
    reg_cmd_addr.addr().set(gpu_addr);

    auto reg_cmd_ctrl = registers::FetchEngineCommandControl::Get().FromValue(0);
    reg_cmd_ctrl.enable().set(1);
    reg_cmd_ctrl.prefetch().set(*prefetch_out);

    auto reg_sec_cmd_ctrl = registers::SecureCommandControl::Get().FromValue(0);
    reg_sec_cmd_ctrl.enable().set(1);
    reg_sec_cmd_ctrl.prefetch().set(*prefetch_out);

    reg_cmd_addr.WriteTo(register_io_.get());
    reg_cmd_ctrl.WriteTo(register_io_.get());
    reg_sec_cmd_ctrl.WriteTo(register_io_.get());

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

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {}
