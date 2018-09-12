// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/scpi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/scpi.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "scpi-internal.h"

// DDK scpi-protocol support
//
// :: Proxies ::
//
// ddk::ScpiProtocolProxy is a simple wrapper around
// scpi_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::ScpiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the scpi protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SCPI device.
// class ScpiDevice {
// using ScpiDeviceType = ddk::Device<ScpiDevice, /* ddk mixins */>;
//
// class ScpiDevice : public ScpiDeviceType,
//                    public ddk::ScpiProtocol<ScpiDevice> {
//   public:
//     ScpiDevice(zx_device_t* parent)
//         : ScpiDeviceType("my-scpi-protocol-device", parent) {}
//
//     zx_status_t ScpiGetSensor(const char* name, uint32_t* out_sensor_id);
//
//     zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* out_sensor_value);
//
//     zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* out_opps);
//
//     zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* out_index);
//
//     zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t index);
//
//     ...
// };

namespace ddk {

template <typename D>
class ScpiProtocol : public internal::base_mixin {
public:
    ScpiProtocol() {
        internal::CheckScpiProtocolSubclass<D>();
        scpi_protocol_ops_.get_sensor = ScpiGetSensor;
        scpi_protocol_ops_.get_sensor_value = ScpiGetSensorValue;
        scpi_protocol_ops_.get_dvfs_info = ScpiGetDvfsInfo;
        scpi_protocol_ops_.get_dvfs_idx = ScpiGetDvfsIdx;
        scpi_protocol_ops_.set_dvfs_idx = ScpiSetDvfsIdx;
    }

protected:
    scpi_protocol_ops_t scpi_protocol_ops_ = {};

private:
    static zx_status_t ScpiGetSensor(void* ctx, const char* name, uint32_t* out_sensor_id) {
        return static_cast<D*>(ctx)->ScpiGetSensor(name, out_sensor_id);
    }
    static zx_status_t ScpiGetSensorValue(void* ctx, uint32_t sensor_id,
                                          uint32_t* out_sensor_value) {
        return static_cast<D*>(ctx)->ScpiGetSensorValue(sensor_id, out_sensor_value);
    }
    static zx_status_t ScpiGetDvfsInfo(void* ctx, uint8_t power_domain, scpi_opp_t* out_opps) {
        return static_cast<D*>(ctx)->ScpiGetDvfsInfo(power_domain, out_opps);
    }
    static zx_status_t ScpiGetDvfsIdx(void* ctx, uint8_t power_domain, uint16_t* out_index) {
        return static_cast<D*>(ctx)->ScpiGetDvfsIdx(power_domain, out_index);
    }
    static zx_status_t ScpiSetDvfsIdx(void* ctx, uint8_t power_domain, uint16_t index) {
        return static_cast<D*>(ctx)->ScpiSetDvfsIdx(power_domain, index);
    }
};

class ScpiProtocolProxy {
public:
    ScpiProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    ScpiProtocolProxy(const scpi_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(scpi_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetSensor(const char* name, uint32_t* out_sensor_id) {
        return ops_->get_sensor(ctx_, name, out_sensor_id);
    }
    zx_status_t GetSensorValue(uint32_t sensor_id, uint32_t* out_sensor_value) {
        return ops_->get_sensor_value(ctx_, sensor_id, out_sensor_value);
    }
    zx_status_t GetDvfsInfo(uint8_t power_domain, scpi_opp_t* out_opps) {
        return ops_->get_dvfs_info(ctx_, power_domain, out_opps);
    }
    zx_status_t GetDvfsIdx(uint8_t power_domain, uint16_t* out_index) {
        return ops_->get_dvfs_idx(ctx_, power_domain, out_index);
    }
    zx_status_t SetDvfsIdx(uint8_t power_domain, uint16_t index) {
        return ops_->set_dvfs_idx(ctx_, power_domain, index);
    }

private:
    scpi_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
