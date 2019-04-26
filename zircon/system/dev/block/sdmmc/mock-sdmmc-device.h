// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "sdmmc-device.h"

#include <lib/mock-function/mock-function.h>

namespace sdmmc {

class MockSdmmcDevice : public SdmmcDevice, public ddk::SdmmcProtocol<MockSdmmcDevice> {
public:
    MockSdmmcDevice(const sdmmc_host_info_t& host_info)
        : SdmmcDevice(ddk::SdmmcProtocolClient(), host_info), mock_host_(&mock_host_proto_) {}

    const ddk::SdmmcProtocolClient& host() const override { return mock_host_; }

    zx_status_t SdmmcRequest(sdmmc_req_t* req) override {
        return mock_sdmmc_request_.Call(req->cmd_idx, req->arg, req->blockcount, req->blocksize);
    }

    zx_status_t SdmmcStopTransmission() override { return mock_sdmmc_stop_transmission_.Call(); }

    zx_status_t SdioIoRwExtended(uint32_t caps, bool write, uint32_t fn_idx, uint32_t reg_addr,
                                 bool incr, uint32_t blk_count, uint32_t blk_size, bool use_dma,
                                 uint8_t* buf, zx_handle_t dma_vmo, uint64_t buf_offset) override {
        return mock_sdio_io_rw_extended_.Call(caps, write, fn_idx, reg_addr, incr, blk_count,
                                              blk_size, buf_offset);
    }

    zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SdmmcSetBusFreq(uint32_t bus_freq) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SdmmcSetTiming(sdmmc_timing_t timing) { return ZX_ERR_NOT_SUPPORTED; }
    void SdmmcHwReset() {}
    zx_status_t SdmmcPerformTuning(uint32_t cmd_idx) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb) {
        return ZX_OK;
    }

    auto& mock_SdmmcRequest() { return mock_sdmmc_request_; }
    auto& mock_SdmmcStopTransmission() { return mock_sdmmc_stop_transmission_; }
    auto& mock_SdioIoRwExtended() { return mock_sdio_io_rw_extended_; }

    void VerifyAll() {
        mock_sdmmc_request_.VerifyAndClear();
        mock_sdmmc_stop_transmission_.VerifyAndClear();
        mock_sdio_io_rw_extended_.VerifyAndClear();
    }

private:
    sdmmc_protocol_t mock_host_proto_ = {&sdmmc_protocol_ops_, this};
    const ddk::SdmmcProtocolClient mock_host_;
    mock_function::MockFunction<zx_status_t, uint32_t, uint32_t, uint16_t, uint16_t>
        mock_sdmmc_request_;
    mock_function::MockFunction<zx_status_t> mock_sdmmc_stop_transmission_;
    mock_function::MockFunction<zx_status_t, uint32_t, bool, uint32_t, uint32_t, bool, uint32_t,
                                uint32_t, uint64_t>
        mock_sdio_io_rw_extended_;
};

}  // namespace sdmmc
