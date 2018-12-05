// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/clk.h>
#include <ddktl/protocol/empty-protocol.h>
#include <zircon/device/thermal.h>

namespace thermal {

class MtkThermal;
using DeviceType = ddk::Device<MtkThermal, ddk::Ioctlable>;

class MtkThermal : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL> {
public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* actual);

private:
    MtkThermal(zx_device_t* parent, ddk::MmioBuffer mmio, ddk::MmioBuffer fuse_mmio,
               ddk::MmioBuffer pll_mmio, ddk::MmioBuffer pmic_mmio,
               const ddk::ClkProtocolProxy& clk, const pdev_device_info_t& info,
               const thermal_device_info_t& thermal_info)
        : DeviceType(parent), mmio_(std::move(mmio)), fuse_mmio_(std::move(fuse_mmio)),
          pll_mmio_(std::move(pll_mmio)), pmic_mmio_(std::move(pmic_mmio)), clk_(clk),
          clk_count_(info.clk_count), thermal_info_(thermal_info) {}

    zx_status_t GetTemperature(uint32_t* temp);
    zx_status_t SetDvfsOpp(const dvfs_info_t* opp);
    zx_status_t Init();

    uint32_t RawToTemperature(uint32_t raw, int sensor);

    uint16_t PmicRead(uint32_t addr);
    void PmicWrite(uint16_t data, uint32_t addr);

    ddk::MmioBuffer mmio_;
    ddk::MmioBuffer fuse_mmio_;
    ddk::MmioBuffer pll_mmio_;
    ddk::MmioBuffer pmic_mmio_;
    ddk::ClkProtocolProxy clk_;
    const uint32_t clk_count_;
    const thermal_device_info_t thermal_info_;
    uint32_t current_opp_idx_ = 0;
};

}  // namespace thermal
