// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <mutex>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>

#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <hw/reg.h>
#include <lib/fidl-utils/bind.h>

#include "magma_util/macros.h"
#include "sys_driver/magma_driver.h"

#define GPU_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

enum {
    // Indices into clocks provided by the board file.
    kClkSlowMfgIndex = 0,
    kClkAxiMfgIndex = 1,
    kClkMfgMmIndex = 2,
    kClockCount,

    // Indices into mmio buffers provided by the board file.
    kMfgMmioIndex = 0,
    kMfgTopMmioIndex = 1,
    kScpsysMmioIndex = 2,
    kXoMmioIndex = 3,

    kInfraTopAxiSi1Ctl = 0x1204,
    kInfraTopAxiProtectEn = 0x1220,
    kInfraTopAxiProtectSta1 = 0x1228,

    kPwrStatus = 0x60c,
    kPwrStatus2nd = 0x610,
};

struct ComponentDescription {
    zx_status_t PowerOn(ddk::MmioBuffer* power_gpu_buffer);
    bool IsPoweredOn(ddk::MmioBuffer* power_gpu_buffer, uint32_t bit);

    // offset into power_gpu_buffer registers
    uint32_t reg_offset;
    // Index into the power status registers, used to determine when powered on.
    uint32_t on_bit_offset;
    // Bits in the register that need to be set to zero to power on the SRAM.
    uint32_t sram_bits;
    // Bits in the register that will be cleared once the SRAM is powered on.
    uint32_t sram_ack_bits;
};

class Mt8167sGpu;

using DeviceType = ddk::Device<Mt8167sGpu, ddk::Messageable>;

class Mt8167sGpu : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_GPU> {
public:
    Mt8167sGpu(zx_device_t* parent) : DeviceType(parent) {}

    ~Mt8167sGpu();

    zx_status_t Bind();
    void DdkRelease();

    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    zx_status_t Query(uint64_t query_id, fidl_txn_t* transaction);
    zx_status_t Connect(uint64_t client_id, fidl_txn_t* transaction);
    zx_status_t DumpState(uint32_t dump_type);
    zx_status_t Restart();

private:
    bool StartMagma() MAGMA_REQUIRES(magma_mutex_);
    void StopMagma() MAGMA_REQUIRES(magma_mutex_);

    // MFG is Mediatek's name for their graphics subsystem.
    zx_status_t PowerOnMfgAsync();
    zx_status_t PowerOnMfg2d();
    zx_status_t PowerOnMfg();

    void EnableMfgHwApm();

    ddk::ClockProtocolClient clks_[kClockCount];
    // MFG TOP MMIO - Controls mediatek's gpu-related power- and
    // clock-management hardware.
    std::optional<ddk::MmioBuffer> gpu_buffer_;
    // MFG MMIO (corresponds to the IMG GPU's registers)
    std::optional<ddk::MmioBuffer> real_gpu_buffer_;
    std::optional<ddk::MmioBuffer> power_gpu_buffer_; // SCPSYS MMIO
    std::optional<ddk::MmioBuffer> clock_gpu_buffer_; // XO MMIO

    std::mutex magma_mutex_;
    std::unique_ptr<MagmaDriver> magma_driver_ MAGMA_GUARDED(magma_mutex_);
    std::shared_ptr<MagmaSystemDevice> magma_system_device_ MAGMA_GUARDED(magma_mutex_);
};

Mt8167sGpu::~Mt8167sGpu()
{
    std::lock_guard<std::mutex> lock(magma_mutex_);
    StopMagma();
}

bool Mt8167sGpu::StartMagma()
{
    magma_system_device_ = magma_driver_->CreateDevice(parent());
    return !!magma_system_device_;
}

void Mt8167sGpu::StopMagma()
{
    if (magma_system_device_) {
        magma_system_device_->Shutdown();
        magma_system_device_.reset();
    }
}

void Mt8167sGpu::DdkRelease() { delete this; }

static fuchsia_gpu_magma_Device_ops_t device_fidl_ops = {
    .Query = fidl::Binder<Mt8167sGpu>::BindMember<&Mt8167sGpu::Query>,
    .Connect = fidl::Binder<Mt8167sGpu>::BindMember<&Mt8167sGpu::Connect>,
    .DumpState = fidl::Binder<Mt8167sGpu>::BindMember<&Mt8167sGpu::DumpState>,
    .TestRestart = fidl::Binder<Mt8167sGpu>::BindMember<&Mt8167sGpu::Restart>,
};

zx_status_t Mt8167sGpu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn)
{
    return fuchsia_gpu_magma_Device_dispatch(this, txn, msg, &device_fidl_ops);
}

zx_status_t ComponentDescription::PowerOn(ddk::MmioBuffer* power_gpu_buffer)
{
    enum {
        kPowerResetBBit = 0,
        kPowerIsoBit = 1,
        kPowerOnBit = 2,
        kPowerOn2ndBit = 3,
        kPowerOnClkDisBit = 4,
    };
    power_gpu_buffer->SetBit<uint32_t>(kPowerOnBit, reg_offset);
    power_gpu_buffer->SetBit<uint32_t>(kPowerOn2ndBit, reg_offset);
    zx::time timeout = zx::deadline_after(zx::msec(100)); // Arbitrary timeout
    while (!IsPoweredOn(power_gpu_buffer, on_bit_offset)) {
        if (zx::clock::get_monotonic() > timeout) {
            GPU_ERROR("Timed out powering on component");
            return ZX_ERR_TIMED_OUT;
        }
    }
    power_gpu_buffer->ClearBit<uint32_t>(kPowerOnClkDisBit, reg_offset);
    power_gpu_buffer->ClearBit<uint32_t>(kPowerIsoBit, reg_offset);
    power_gpu_buffer->SetBit<uint32_t>(kPowerResetBBit, reg_offset);
    if (sram_bits) {
        power_gpu_buffer->ClearBits32(sram_bits, reg_offset);
        zx::time timeout = zx::deadline_after(zx::msec(100)); // Arbitrary timeout
        while (power_gpu_buffer->ReadMasked32(sram_ack_bits, reg_offset)) {
            if (zx::clock::get_monotonic() > timeout) {
                GPU_ERROR("Timed out powering on SRAM");
                return ZX_ERR_TIMED_OUT;
            }
        }
    }
    return ZX_OK;
}

bool ComponentDescription::IsPoweredOn(ddk::MmioBuffer* power_gpu_buffer, uint32_t bit)
{
    return power_gpu_buffer->GetBit<uint32_t>(bit, kPwrStatus) &&
           power_gpu_buffer->GetBit<uint32_t>(bit, kPwrStatus2nd);
}

// Power on the asynchronous memory interface between the GPU and the DDR controller.
zx_status_t Mt8167sGpu::PowerOnMfgAsync()
{
    // Set clock sources properly. Some of these are also used by the 3D and 2D
    // cores.
    clock_gpu_buffer_->ModifyBits<uint32_t>(0, 20, 2, 0x40); // slow mfg mux to 26MHz
    // MFG AXI to mainpll_d11 (on version 2+ of chip)
    clock_gpu_buffer_->ModifyBits<uint32_t>(1, 18, 2, 0x40);
    clks_[kClkSlowMfgIndex].Enable();
    clks_[kClkAxiMfgIndex].Enable();
    constexpr uint32_t kAsyncPwrStatusBit = 25;
    constexpr uint32_t kAsyncPwrRegOffset = 0x2c4;
    ComponentDescription mfg_async = {kAsyncPwrRegOffset, kAsyncPwrStatusBit, 0, 0};
    return mfg_async.PowerOn(&power_gpu_buffer_.value());
}

// Power on the 2D engine (it's unclear whether this is needed to access the 3D
// GPU, but power it on anyway).
zx_status_t Mt8167sGpu::PowerOnMfg2d()
{
    // Enable access to AXI Bus
    clock_gpu_buffer_->SetBits32((1 << 7), kInfraTopAxiSi1Ctl);
    constexpr uint32_t k2dPwrStatusBit = 24;
    constexpr uint32_t k2dPwrRegOffset = 0x2c0;

    ComponentDescription mfg_2d = {k2dPwrRegOffset, k2dPwrStatusBit, 0xf << 8, 0xf << 12};
    zx_status_t status = mfg_2d.PowerOn(&power_gpu_buffer_.value());
    if (status != ZX_OK)
        return status;
    // Disable AXI protection after it's powered up.
    clock_gpu_buffer_->ClearBits32((1 << 2) | (1 << 5), kInfraTopAxiProtectEn);
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
    return ZX_OK;
}

// Power on the 3D engine (IMG GPU).
zx_status_t Mt8167sGpu::PowerOnMfg()
{
    clks_[kClkMfgMmIndex].Enable();
    static constexpr uint32_t kMfg3dPwrCon = 0x214;
    ComponentDescription mfg = {kMfg3dPwrCon, 4, 0xf << 8, 0xf << 12};
    zx_status_t status = mfg.PowerOn(&power_gpu_buffer_.value());
    if (status != ZX_OK)
        return status;

    // Power on MFG (internal to TOP)

    constexpr uint32_t kMfgCgClr = 0x8;
    constexpr uint32_t kBAxiClr = (1 << 0);
    constexpr uint32_t kBMemClr = (1 << 1);
    constexpr uint32_t kBG3dClr = (1 << 2);
    constexpr uint32_t kB26MClr = (1 << 3);
    gpu_buffer_->SetBits32(kBAxiClr | kBMemClr | kBG3dClr | kB26MClr, kMfgCgClr);
    EnableMfgHwApm();
    return ZX_OK;
}

// Enable hardware-controlled power management.
void Mt8167sGpu::EnableMfgHwApm()
{
    struct {
        uint32_t value;
        uint32_t offset;
    } writes[] = {
        {0x01a80000, 0x504}, {0x00080010, 0x508}, {0x00080010, 0x50c}, {0x00b800b8, 0x510},
        {0x00b000b0, 0x514}, {0x00c000c8, 0x518}, {0x00c000c8, 0x51c}, {0x00d000d8, 0x520},
        {0x00d800d8, 0x524}, {0x00d800d8, 0x528}, {0x9000001b, 0x24},  {0x8000001b, 0x24},
    };

    for (uint32_t i = 0; i < countof(writes); i++) {
        gpu_buffer_->Write32(writes[i].value, writes[i].offset);
    }
}

static uint64_t ReadHW64(const ddk::MmioBuffer* buffer, uint32_t offset)
{
    // Read 2 registers to combine into a 64-bit register.
    return (static_cast<uint64_t>(buffer->Read32(offset + 4)) << 32) | buffer->Read32(offset);
}

zx_status_t Mt8167sGpu::Bind()
{
    pdev_protocol_t pdev_proto;
    zx_status_t status;

    if ((status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev_proto)) != ZX_OK) {
        GPU_ERROR("ZX_PROTOCOL_PDEV not available\n");
        return status;
    }

    ddk::PDev pdev(&pdev_proto);

    for (unsigned i = 0; i < kClockCount; i++) {
        clks_[i] = pdev.GetClk(i);
        if (!clks_[i].is_valid()) {
            zxlogf(ERROR, "%s GetClk failed %d\n", __func__, status);
            return status;
        }
    }

    status = pdev.MapMmio(kMfgMmioIndex, &real_gpu_buffer_);
    if (status != ZX_OK) {
        GPU_ERROR("pdev_map_mmio_buffer failed\n");
        return status;
    }
    status = pdev.MapMmio(kMfgTopMmioIndex, &gpu_buffer_);
    if (status != ZX_OK) {
        GPU_ERROR("pdev_map_mmio_buffer failed\n");
        return status;
    }
    status = pdev.MapMmio(kScpsysMmioIndex, &power_gpu_buffer_);
    if (status != ZX_OK) {
        GPU_ERROR("pdev_map_mmio_buffer failed\n");
        return status;
    }
    status = pdev.MapMmio(kXoMmioIndex, &clock_gpu_buffer_);
    if (status != ZX_OK) {
        GPU_ERROR("pdev_map_mmio_buffer failed\n");
        return status;
    }

    // Power on in order.
    status = PowerOnMfgAsync();
    if (status != ZX_OK) {
        GPU_ERROR("Failed to power on MFG ASYNC\n");
        return status;
    }
    status = PowerOnMfg2d();
    if (status != ZX_OK) {
        GPU_ERROR("Failed to power on MFG 2D\n");
        return status;
    }
    status = PowerOnMfg();
    if (status != ZX_OK) {
        GPU_ERROR("Failed to power on MFG\n");
        return status;
    }
    zxlogf(INFO, "[mt8167s-gpu] GPU ID: %lx\n", ReadHW64(&real_gpu_buffer_.value(), 0x18));
    zxlogf(INFO, "[mt8167s-gpu] GPU core revision: %lx\n",
           ReadHW64(&real_gpu_buffer_.value(), 0x20));

    {
        std::lock_guard<std::mutex> lock(magma_mutex_);
        magma_driver_ = MagmaDriver::Create();
        if (!magma_driver_) {
            GPU_ERROR("Failed to create MagmaDriver\n");
            return ZX_ERR_INTERNAL;
        }

        if (!StartMagma()) {
            GPU_ERROR("Failed to start Magma system device\n");
            return ZX_ERR_INTERNAL;
        }
    }

    return DdkAdd("mt8167s-gpu");
}

zx_status_t Mt8167sGpu::Query(uint64_t query_id, fidl_txn_t* transaction)
{
    DLOG("Mt8167sGpu::Query");
    std::lock_guard<std::mutex> lock(magma_mutex_);

    uint64_t result;
    switch (query_id) {
        case MAGMA_QUERY_DEVICE_ID:
            result = magma_system_device_->GetDeviceId();
            break;
        case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
#if MAGMA_TEST_DRIVER
            result = 1;
#else
            result = 0;
#endif
            break;
        default:
            if (!magma_system_device_->Query(query_id, &result))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, result);
    }
    DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);

    zx_status_t status = fuchsia_gpu_magma_DeviceQuery_reply(transaction, result);
    if (status != ZX_OK)
        return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQuery_reply failed: %d", status);
    return ZX_OK;
}

zx_status_t Mt8167sGpu::Connect(uint64_t client_id, fidl_txn_t* transaction)
{
    DLOG("Mt8167sGpu::Connect");
    std::lock_guard<std::mutex> lock(magma_mutex_);

    auto connection = MagmaSystemDevice::Open(magma_system_device_, client_id);
    if (!connection)
        return DRET_MSG(ZX_ERR_INVALID_ARGS, "MagmaSystemDevice::Open failed");

    zx_status_t status = fuchsia_gpu_magma_DeviceConnect_reply(
        transaction, connection->GetClientEndpoint(), connection->GetClientNotificationEndpoint());
    if (status != ZX_OK)
        return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceConnect_reply failed: %d", status);

    magma_system_device_->StartConnectionThread(std::move(connection));
    return ZX_OK;
}

zx_status_t Mt8167sGpu::DumpState(uint32_t dump_type)
{
    DLOG("Mt8167sGpu::DumpState");
    std::lock_guard<std::mutex> lock(magma_mutex_);
    if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                      MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE))
        return DRET_MSG(ZX_ERR_INVALID_ARGS, "Invalid dump type %x", dump_type);

    if (magma_system_device_)
        magma_system_device_->DumpStatus(dump_type);
    return ZX_OK;
}

zx_status_t Mt8167sGpu::Restart()
{
    DLOG("Mt8167sGpu::Restart");
#if MAGMA_TEST_DRIVER
    std::lock_guard<std::mutex> lock(magma_mutex_);
    StopMagma();
    if (!StartMagma()) {
        return DRET_MSG(ZX_ERR_INTERNAL, "StartMagma failed\n");
    }
    return ZX_OK;
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
}

extern "C" zx_status_t mt8167s_gpu_bind(void* ctx, zx_device_t* parent)
{
    auto dev = std::make_unique<Mt8167sGpu>(parent);
    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        dev.release();
    }
    return status;
}

static zx_driver_ops_t mt8167s_gpu_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = mt8167s_gpu_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(mt8167s_gpu, mt8167s_gpu_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_GPU),
ZIRCON_DRIVER_END(mt8167s_gpu)
