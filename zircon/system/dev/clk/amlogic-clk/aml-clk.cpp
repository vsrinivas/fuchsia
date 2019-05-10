// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-clk.h"
#include "aml-axg-blocks.h"
#include "aml-g12a-blocks.h"
#include "aml-g12b-blocks.h"
#include "aml-gxl-blocks.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/pdev.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <string.h>

namespace amlogic_clock {

// MMIO Indexes
static constexpr uint32_t kHiuMmio = 0;
static constexpr uint32_t kMsrMmio = 1;

#define MSR_WAIT_BUSY_RETRIES 5
#define MSR_WAIT_BUSY_TIMEOUT_US 10000

AmlClock::AmlClock(zx_device_t* device,
                   ddk::MmioBuffer hiu_mmio,
                   std::optional<ddk::MmioBuffer> msr_mmio,
                   uint32_t device_id)
    : DeviceType(device)
    , hiu_mmio_(std::move(hiu_mmio))
    , msr_mmio_(std::move(msr_mmio)) {
    // Populate the correct register blocks.
    switch (device_id) {
    case PDEV_DID_AMLOGIC_AXG_CLK: {
        gates_ = axg_clk_gates;
        gate_count_ = countof(axg_clk_gates);
        break;
    }
    case PDEV_DID_AMLOGIC_GXL_CLK: {
        gates_ = gxl_clk_gates;
        gate_count_ = countof(gxl_clk_gates);
        break;
    }
    case PDEV_DID_AMLOGIC_G12A_CLK: {
        clk_msr_offsets_ = g12a_clk_msr;

        clk_table_ = static_cast<const char* const*>(g12a_clk_table);
        clk_table_count_ = countof(g12a_clk_table);

        gates_ = g12a_clk_gates;
        gate_count_ = countof(g12a_clk_gates);
        break;
    }
    case PDEV_DID_AMLOGIC_G12B_CLK: {
        clk_msr_offsets_ = g12b_clk_msr;

        clk_table_ = static_cast<const char* const*>(g12b_clk_table);
        clk_table_count_ = countof(g12b_clk_table);

        gates_ = g12b_clk_gates;
        gate_count_ = countof(g12b_clk_gates);
        break;
    }
    default:
        ZX_PANIC("aml-clk: Unsupported SOC DID %u\n", device_id);
    }
}

zx_status_t AmlClock::Init(uint32_t did) {
    pbus_protocol_t pbus;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: failed to get ZX_PROTOCOL_PBUS, st = %d\n",
               status);
        return status;
    }

    clock_impl_protocol_t clk_proto = {
        .ops = &clock_impl_protocol_ops_,
        .ctx = this,
    };

    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: pbus_register_protocol failed, st = %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t AmlClock::Create(zx_device_t* parent) {
    zx_status_t status;

    // Get the platform device protocol and try to map all the MMIO regions.
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "aml-clk: failed to get pdev protocol\n");
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> hiu_mmio = std::nullopt;
    std::optional<ddk::MmioBuffer> msr_mmio = std::nullopt;

    // All AML clocks have HIU regs but only some support MSR regs.
    // Figure out which of the varieties we're dealing with.
    status = pdev.MapMmio(kHiuMmio, &hiu_mmio);
    if (status != ZX_OK || !hiu_mmio) {
        zxlogf(ERROR, "aml-clk: failed to map HIU regs, status = %d\n", status);
        return status;
    }

    // Use the Pdev Device Info to determine if we've been provided with two
    // MMIO regions.
    pdev_device_info_t info;
    status = pdev.GetDeviceInfo(&info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: failed to get pdev device info, status = %d\n", status);
        return status;
    }

    if (info.mmio_count > 1) {
        status = pdev.MapMmio(kMsrMmio, &msr_mmio);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-clk: failed to map MSR regs, status = %d\n", status);
            return status;
        }
    }

    auto clock_device =
        std::make_unique<amlogic_clock::AmlClock>(
            parent,
            std::move(*hiu_mmio),
            *std::move(msr_mmio),
            info.did
        );

    status = clock_device->Init(info.did);
    if (status != ZX_OK) {
        return status;
    }

    status = clock_device->DdkAdd("clocks");
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: Could not create clock device: %d\n", status);
        return status;
    }

    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = clock_device.release();
    return ZX_OK;
}

zx_status_t AmlClock::ClkToggle(uint32_t clk, const bool enable) {
    if (clk >= gate_count_) {
        return ZX_ERR_INVALID_ARGS;
    }

    const meson_clk_gate_t* gate = &(gates_[clk]);

    fbl::AutoLock al(&lock_);

    if (enable) {
        hiu_mmio_.SetBits32(1 << gate->bit, gate->reg);
    } else {
        hiu_mmio_.ClearBits32(1 << gate->bit, gate->reg);
    }

    return ZX_OK;
}

zx_status_t AmlClock::ClockImplEnable(uint32_t clk) {
    return ClkToggle(clk, true);
}

zx_status_t AmlClock::ClockImplDisable(uint32_t clk) {
    return ClkToggle(clk, false);
}

zx_status_t AmlClock::ClockImplRequestRate(uint32_t id, uint64_t hz) {
    return ZX_ERR_NOT_SUPPORTED;
}

// Note: The clock index taken here are the index of clock
// from the clock table and not the clock_gates index.
// This API measures the clk frequency for clk.
// Following implementation is adopted from Amlogic SDK,
// there is absolutely no documentation.
zx_status_t AmlClock::ClkMeasureUtil(uint32_t clk, uint64_t* clk_freq) {
    if (!msr_mmio_) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Set the measurement gate to 64uS.
    uint32_t value = 64 - 1;
    msr_mmio_->Write32(value, clk_msr_offsets_.reg0_offset);
    // Disable continuous measurement.
    // Disable interrupts.
    value = MSR_CONT | MSR_INTR;
    // Clear the clock source.
    value |= MSR_CLK_SRC_MASK << MSR_CLK_SRC_SHIFT;
    msr_mmio_->ClearBits32(value, clk_msr_offsets_.reg0_offset);

    value = ((clk << MSR_CLK_SRC_SHIFT) | // Select the MUX.
             MSR_RUN |                    // Enable the clock.
             MSR_ENABLE);                 // Enable measuring.
    msr_mmio_->SetBits32(value, clk_msr_offsets_.reg0_offset);

    // Wait for the measurement to be done.
    for (uint32_t i = 0; i < MSR_WAIT_BUSY_RETRIES; i++) {
        value = msr_mmio_->Read32(clk_msr_offsets_.reg0_offset);
        if (value & MSR_BUSY) {
            // Wait a little bit before trying again.
            zx_nanosleep(zx_deadline_after(ZX_USEC(MSR_WAIT_BUSY_TIMEOUT_US)));
            continue;
        } else {
            // Disable measuring.
            msr_mmio_->ClearBits32(MSR_ENABLE, clk_msr_offsets_.reg0_offset);
            // Get the clock value.
            value = msr_mmio_->Read32(clk_msr_offsets_.reg2_offset);
            // Magic numbers, since lack of documentation.
            *clk_freq = (((value + 31) & MSR_VAL_MASK) / 64);
            return ZX_OK;
        }
    }
    return ZX_ERR_TIMED_OUT;
}

zx_status_t AmlClock::ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info) {
    if (clk >= clk_table_count_) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t max_len = sizeof(info->name);
    size_t len = strnlen(clk_table_[clk], max_len);
    if (len == max_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(info->name, clk_table_[clk], len + 1);
    return ClkMeasureUtil(clk, &info->frequency);
}

uint32_t AmlClock::GetClkCount() {
    return static_cast<uint32_t>(clk_table_count_);
}

void AmlClock::ShutDown() {
    hiu_mmio_.reset();

    if (msr_mmio_) {
        msr_mmio_->reset();
    }
}

zx_status_t fidl_clk_measure(void* ctx, uint32_t clk, fidl_txn_t* txn) {
    auto dev = static_cast<AmlClock*>(ctx);
    fuchsia_hardware_clock_FrequencyInfo info;

    dev->ClkMeasure(clk, &info);

    return fuchsia_hardware_clock_DeviceMeasure_reply(txn, &info);
}

zx_status_t fidl_clk_get_count(void* ctx, fidl_txn_t* txn) {
    auto dev = static_cast<AmlClock*>(ctx);

    return fuchsia_hardware_clock_DeviceGetCount_reply(txn, dev->GetClkCount());
}

static const fuchsia_hardware_clock_Device_ops_t fidl_ops_ = {
    .Measure = fidl_clk_measure,
    .GetCount = fidl_clk_get_count,
};

zx_status_t AmlClock::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_clock_Device_dispatch(this, txn, msg, &fidl_ops_);
}

void AmlClock::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void AmlClock::DdkRelease() {
    delete this;
}

} // namespace amlogic_clock

zx_status_t aml_clk_bind(void* ctx, zx_device_t* parent) {
    return amlogic_clock::AmlClock::Create(parent);
}

static constexpr zx_driver_ops_t aml_clk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = aml_clk_bind;
    return ops;
}();


// clang-format off
ZIRCON_DRIVER_BEGIN(aml_clk, aml_clk_driver_ops, "zircon", "0.1", 6)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    // we support multiple SOC variants.
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_AXG_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GXL_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_G12A_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_G12B_CLK),
ZIRCON_DRIVER_END(aml_clk)
