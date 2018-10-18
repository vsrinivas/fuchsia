// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdmmc.banjo INSTEAD.

#pragma once

#include <ddk/protocol/sdmmc.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "sdmmc-internal.h"

// DDK sdmmc-protocol support
//
// :: Proxies ::
//
// ddk::SdmmcProtocolProxy is a simple wrapper around
// sdmmc_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::SdmmcProtocol is a mixin class that simplifies writing DDK drivers
// that implement the sdmmc protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SDMMC device.
// class SdmmcDevice {
// using SdmmcDeviceType = ddk::Device<SdmmcDevice, /* ddk mixins */>;
//
// class SdmmcDevice : public SdmmcDeviceType,
//                     public ddk::SdmmcProtocol<SdmmcDevice> {
//   public:
//     SdmmcDevice(zx_device_t* parent)
//         : SdmmcDeviceType("my-sdmmc-protocol-device", parent) {}
//
//     zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info);
//
//     zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
//
//     zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
//
//     zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
//
//     zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
//
//     void SdmmcHwReset();
//
//     zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
//
//     zx_status_t SdmmcRequest(sdmmc_req_t* req);
//
//     ...
// };

namespace ddk {

template <typename D>
class SdmmcProtocol : public internal::base_protocol {
public:
    SdmmcProtocol() {
        internal::CheckSdmmcProtocolSubclass<D>();
        ops_.host_info = SdmmcHostInfo;
        ops_.set_signal_voltage = SdmmcSetSignalVoltage;
        ops_.set_bus_width = SdmmcSetBusWidth;
        ops_.set_bus_freq = SdmmcSetBusFreq;
        ops_.set_timing = SdmmcSetTiming;
        ops_.hw_reset = SdmmcHwReset;
        ops_.perform_tuning = SdmmcPerformTuning;
        ops_.request = SdmmcRequest;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SDMMC;
        ddk_proto_ops_ = &ops_;
    }

protected:
    sdmmc_protocol_ops_t ops_ = {};

private:
    // Get host info.
    static zx_status_t SdmmcHostInfo(void* ctx, sdmmc_host_info_t* out_info) {
        return static_cast<D*>(ctx)->SdmmcHostInfo(out_info);
    }
    // Set signal voltage.
    static zx_status_t SdmmcSetSignalVoltage(void* ctx, sdmmc_voltage_t voltage) {
        return static_cast<D*>(ctx)->SdmmcSetSignalVoltage(voltage);
    }
    // Set bus width.
    static zx_status_t SdmmcSetBusWidth(void* ctx, sdmmc_bus_width_t bus_width) {
        return static_cast<D*>(ctx)->SdmmcSetBusWidth(bus_width);
    }
    // Set bus frequency.
    static zx_status_t SdmmcSetBusFreq(void* ctx, uint32_t bus_freq) {
        return static_cast<D*>(ctx)->SdmmcSetBusFreq(bus_freq);
    }
    // Set mmc timing.
    static zx_status_t SdmmcSetTiming(void* ctx, sdmmc_timing_t timing) {
        return static_cast<D*>(ctx)->SdmmcSetTiming(timing);
    }
    // Issue a hw reset.
    static void SdmmcHwReset(void* ctx) { static_cast<D*>(ctx)->SdmmcHwReset(); }
    // Perform tuning.
    static zx_status_t SdmmcPerformTuning(void* ctx, uint32_t cmd_idx) {
        return static_cast<D*>(ctx)->SdmmcPerformTuning(cmd_idx);
    }
    // Issue a request.
    static zx_status_t SdmmcRequest(void* ctx, sdmmc_req_t* req) {
        return static_cast<D*>(ctx)->SdmmcRequest(req);
    }
};

class SdmmcProtocolProxy {
public:
    SdmmcProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    SdmmcProtocolProxy(const sdmmc_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(sdmmc_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Get host info.
    zx_status_t HostInfo(sdmmc_host_info_t* out_info) { return ops_->host_info(ctx_, out_info); }
    // Set signal voltage.
    zx_status_t SetSignalVoltage(sdmmc_voltage_t voltage) {
        return ops_->set_signal_voltage(ctx_, voltage);
    }
    // Set bus width.
    zx_status_t SetBusWidth(sdmmc_bus_width_t bus_width) {
        return ops_->set_bus_width(ctx_, bus_width);
    }
    // Set bus frequency.
    zx_status_t SetBusFreq(uint32_t bus_freq) { return ops_->set_bus_freq(ctx_, bus_freq); }
    // Set mmc timing.
    zx_status_t SetTiming(sdmmc_timing_t timing) { return ops_->set_timing(ctx_, timing); }
    // Issue a hw reset.
    void HwReset() { ops_->hw_reset(ctx_); }
    // Perform tuning.
    zx_status_t PerformTuning(uint32_t cmd_idx) { return ops_->perform_tuning(ctx_, cmd_idx); }
    // Issue a request.
    zx_status_t Request(sdmmc_req_t* req) { return ops_->request(ctx_, req); }

private:
    sdmmc_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
