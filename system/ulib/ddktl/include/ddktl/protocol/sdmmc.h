// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/sdmmc.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "sdmmc-internal.h"

// DDK SDMMC protocol support.
//
// :: Mixins ::
//
// ddk::SdMmcProtocol is a mixin class that simplifies writing DDK drivers that implement the SDMMC
// protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SDMMC device.
// class SdMmcDevice;
// using SdMmcDeviceType = ddk::Device<SdMmcDevice, /* ddk mixins */>;
//
// class SdMmcDevice : public SdMmcDeviceType,
//                     public ddk::SdMmcProtocol<SdMmcDevice> {
//   public:
//     SdMmcDevice(zx_device_t* parent)
//       : SdMmcDeviceType(parent) {}
//
//     zx_status_t HostInfo(sdmmc_host_info_t* info);
//     zx_status_t SetSignalVoltage(sdmmc_voltage_t voltage);
//     zx_status_t SetBusWidth(sdmmc_bus_width_t bus_width);
//     zx_status_t SetBusFreq(uint32_t bus_freq);
//     zx_status_t SetTiming(sdmmc_timing_t timing);
//     void HwReset();
//     zx_status_t PerformTuning(uint32_t cmd_idx);
//     zx_status_t Request(sdmmc_req_t* req);
//     ...
// };

namespace ddk {

template <typename D>
class SdMmcProtocol : public internal::base_protocol {
public:
    SdMmcProtocol() {
        internal::CheckSdMmcProtocolSubclass<D>();
        sdmmc_proto_ops_.host_info = HostInfo;
        sdmmc_proto_ops_.set_signal_voltage = SetSignalVoltage;
        sdmmc_proto_ops_.set_bus_width = SetBusWidth;
        sdmmc_proto_ops_.set_bus_freq = SetBusFreq;
        sdmmc_proto_ops_.set_timing = SetTiming;
        sdmmc_proto_ops_.hw_reset = HwReset;
        sdmmc_proto_ops_.perform_tuning = PerformTuning;
        sdmmc_proto_ops_.request = Request;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SDMMC;
        ddk_proto_ops_ = &sdmmc_proto_ops_;
    }

protected:
    sdmmc_protocol_ops_t sdmmc_proto_ops_ = {};

private:
    static zx_status_t HostInfo(void* ctx, sdmmc_host_info_t* info) {
        return static_cast<D*>(ctx)->HostInfo(info);
    }

    static zx_status_t SetSignalVoltage(void* ctx, sdmmc_voltage_t voltage) {
        return static_cast<D*>(ctx)->SetSignalVoltage(voltage);
    }

    static zx_status_t SetBusWidth(void* ctx, sdmmc_bus_width_t bus_width) {
        return static_cast<D*>(ctx)->SetBusWidth(bus_width);
    }

    static zx_status_t SetBusFreq(void* ctx, uint32_t bus_freq) {
        return static_cast<D*>(ctx)->SetBusFreq(bus_freq);
    }

    static zx_status_t SetTiming(void* ctx, sdmmc_timing_t timing) {
        return static_cast<D*>(ctx)->SetTiming(timing);
    }

    static void HwReset(void* ctx) {
        static_cast<D*>(ctx)->HwReset();
    }

    static zx_status_t PerformTuning(void* ctx, uint32_t cmd_idx) {
        return static_cast<D*>(ctx)->PerformTuning(cmd_idx);
    }

    static zx_status_t Request(void* ctx, sdmmc_req_t* req) {
        return static_cast<D*>(ctx)->Request(req);
    }
};

}  // namespace ddk
