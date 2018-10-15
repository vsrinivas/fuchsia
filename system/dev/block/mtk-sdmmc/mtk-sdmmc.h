// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/sdmmc.h>

namespace sdmmc {

class MtkSdmmc;
using DeviceType = ddk::Device<MtkSdmmc>;

class MtkSdmmc : public DeviceType, public ddk::SdmmcProtocol<MtkSdmmc> {

public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t SdmmcHostInfo(sdmmc_host_info_t* info);
    zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
    zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
    zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
    zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
    void SdmmcHwReset();
    zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
    zx_status_t SdmmcRequest(sdmmc_req_t* req);

private:
    MtkSdmmc(zx_device_t* parent, ddk::MmioBuffer mmio, zx::bti bti, const sdmmc_host_info_t& info)
        : DeviceType(parent), mmio_(fbl::move(mmio)), bti_(fbl::move(bti)), info_(info) {}

    void Init();

    // Prepares the VMO and the DMA engine for receiving data.
    zx_status_t RequestPrepareDma(sdmmc_req_t* req);
    // Waits for the DMA engine to finish and unpins the VMO pages.
    zx_status_t ReadResponseDma(sdmmc_req_t* req);

    // Clears the FIFO in preparation for receiving data.
    zx_status_t RequestPreparePolled(sdmmc_req_t* req);
    // Polls the FIFO register for received data.
    zx_status_t ReadResponsePolled(sdmmc_req_t* req);

    ddk::MmioBuffer mmio_;
    zx::bti bti_;
    const sdmmc_host_info_t info_;
    zx::pmt pmt_;
};

}  // namespace sdmmc
