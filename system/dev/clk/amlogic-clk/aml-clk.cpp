// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-clk.h"
#include "aml-axg-blocks.h"
#include "aml-g12a-blocks.h"
#include "aml-g12b-blocks.h"
#include "aml-gxl-blocks.h"
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <string.h>

namespace amlogic_clock {

// MMIO Indexes
static constexpr uint32_t kHiuMmio = 0;
static constexpr uint32_t kMsrClk = 1;

#define MSR_WAIT_BUSY_RETRIES 5
#define MSR_WAIT_BUSY_TIMEOUT_US 10000

zx_status_t AmlClock::InitHiuRegs(pdev_device_info_t* info) {
    // Map the HIU registers.
    mmio_buffer_t mmio;
    zx_status_t status = pdev_map_mmio_buffer2(&pdev_, kHiuMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                               &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: could not map periph mmio: %d\n", status);
        return status;
    }
    hiu_mmio_ = ddk::MmioBuffer(mmio);

    return ZX_OK;
}

zx_status_t AmlClock::InitMsrRegs(pdev_device_info_t* info) {
    // Map the MSR registers.
    mmio_buffer_t mmio;
    zx_status_t status = pdev_map_mmio_buffer2(&pdev_, kMsrClk, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                               &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: could not map periph mmio: %d\n", status);
        return status;
    }
    msr_mmio_ = ddk::MmioBuffer(mmio);

    return ZX_OK;
}

zx_status_t AmlClock::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent,
                                             ZX_PROTOCOL_PLATFORM_DEV,
                                             &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    // Get the device information.
    pdev_device_info_t info;
    status = pdev_get_device_info(&pdev_, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: pdev_get_device_info failed\n");
        return status;
    }

    status = InitHiuRegs(&info);
    if (status != ZX_OK) {
        return status;
    }

    // If there are more than 1 MMIO range, then this board also
    // has the clock measure hw block. So we check here if it's
    // available and map it only if it exists.
    if (info.mmio_count > 1) {
        // Map the CLK MSR registers.
        status = InitMsrRegs(&info);
        if (status != ZX_OK) {
            return status;
        }
    }

    meson_clk_gate_t* clk_gates;

    // Populate the correct register blocks.
    switch (info.did) {
    case PDEV_DID_AMLOGIC_AXG_CLK: {
        clk_gates = (meson_clk_gate_t*)calloc(fbl::count_of(axg_clk_gates),
                                              sizeof(meson_clk_gate_t));
        if (clk_gates == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(clk_gates, axg_clk_gates, sizeof(meson_clk_gate_t) * fbl::count_of(axg_clk_gates));

        gates_.reset(clk_gates, fbl::count_of(axg_clk_gates));
        clk_msr_ = false;
        break;
    }
    case PDEV_DID_AMLOGIC_GXL_CLK: {
        clk_gates = (meson_clk_gate_t*)calloc(fbl::count_of(gxl_clk_gates),
                                              sizeof(meson_clk_gate_t));
        if (clk_gates == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(clk_gates, gxl_clk_gates, sizeof(meson_clk_gate_t) * fbl::count_of(axg_clk_gates));

        gates_.reset(clk_gates, fbl::count_of(gxl_clk_gates));
        clk_msr_ = false;
        break;
    }
    case PDEV_DID_AMLOGIC_G12A_CLK: {
        clk_msr_offsets_ = g12a_clk_msr;
        clk_table_.reset(g12a_clk_table, fbl::count_of(g12a_clk_table));
        clk_gates = (meson_clk_gate_t*)calloc(fbl::count_of(g12a_clk_gates),
                                              sizeof(meson_clk_gate_t));
        if (clk_gates == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(clk_gates, g12a_clk_gates, sizeof(meson_clk_gate_t) * fbl::count_of(g12a_clk_gates));

        gates_.reset(clk_gates, fbl::count_of(g12a_clk_gates));
        break;
    }
    case PDEV_DID_AMLOGIC_G12B_CLK: {
        clk_msr_offsets_ = g12b_clk_msr;
        clk_table_.reset(g12b_clk_table, fbl::count_of(g12b_clk_table));
        clk_gates = (meson_clk_gate_t*)calloc(fbl::count_of(g12b_clk_gates),
                                              sizeof(meson_clk_gate_t));
        if (clk_gates == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(clk_gates, g12b_clk_gates, sizeof(meson_clk_gate_t) * fbl::count_of(g12b_clk_gates));

        gates_.reset(clk_gates, fbl::count_of(g12b_clk_gates));
        break;
    }
    default:
        zxlogf(ERROR, "aml-clk: Unsupported SOC DID %u\n", info.pid);
        return ZX_ERR_INVALID_ARGS;
    }

    platform_bus_protocol_t pbus;
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: failed to get ZX_PROTOCOL_PLATFORM_BUS, "
                      "st = %d\n",
               status);
        return status;
    }

    clk_protocol_t clk_proto = {
        .ops = &ops_,
        .ctx = this,
    };

    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLK, &clk_proto, NULL, NULL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: pbus_register_protocol failed, st = %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t AmlClock::Create(zx_device_t* parent) {
    auto clock_device = fbl::make_unique<amlogic_clock::AmlClock>(parent);

    zx_status_t status = clock_device->InitPdev(parent);
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
    if (clk >= gates_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    const meson_clk_gate_t* gate = &gates_[clk];
    fbl::AutoLock al(&lock_);

    if (enable) {
        hiu_mmio_->SetBits32(1 << gate->bit, gate->reg);
    } else {
        hiu_mmio_->ClearBits32(1 << gate->bit, gate->reg);
    }

    return ZX_OK;
}

zx_status_t AmlClock::ClkEnable(uint32_t clk) {
    if (clk_gates_) {
        return ClkToggle(clk, true);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t AmlClock::ClkDisable(uint32_t clk) {
    if (clk_gates_) {
        return ClkToggle(clk, false);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

// Note: The clock index taken here are the index of clock
// from the clock table and not the clock_gates index.
// This API measures the clk frequency for clk.
// Following implementation is adopted from Amlogic SDK,
// there is absolutely no documentation.
zx_status_t AmlClock::ClkMeasureUtil(uint32_t clk, uint32_t* clk_freq) {
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

zx_status_t AmlClock::ClkMeasure(uint32_t clk, clk_freq_info_t* info) {
    if (clk >= clk_table_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t max_len = sizeof(info->clk_name);
    size_t len = strnlen(clk_table_[clk], max_len);
    if (len == max_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(info->clk_name, clk_table_[clk], len + 1);
    return ClkMeasureUtil(clk, &info->clk_freq);
}

void AmlClock::ShutDown() {
    hiu_mmio_.reset();
    msr_mmio_.reset();
}

zx_status_t AmlClock::DdkIoctl(uint32_t op, const void* in_buf,
                               size_t in_len, void* out_buf,
                               size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_CLK_MEASURE: {
        if (in_buf == nullptr || in_len != sizeof(uint32_t) ||
            out_buf == nullptr || out_len != sizeof(clk_freq_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto index = *(static_cast<const uint32_t*>(in_buf));
        auto* info = static_cast<clk_freq_info_t*>(out_buf);
        if (clk_msr_) {
            *out_actual = sizeof(clk_freq_info_t);
            return ClkMeasure(index, info);
        } else {
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    case IOCTL_CLK_GET_COUNT: {
        if (out_buf == nullptr || out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto* num_count = static_cast<uint32_t*>(out_buf);
        *num_count = static_cast<uint32_t>(clk_table_.size());
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void AmlClock::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void AmlClock::DdkRelease() {
    delete this;
}

} // namespace amlogic_clock

extern "C" zx_status_t aml_clk_bind(void* ctx, zx_device_t* parent) {
    return amlogic_clock::AmlClock::Create(parent);
}
