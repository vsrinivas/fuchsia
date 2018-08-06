// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/scpi.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "scpi-internal.h"

// DDK SCPI protocol support.
//
// :: Proxies ::
//
// ddk::ScpiProtocolProxy is a simple wrappers around scpi_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ScpiProtocol is a mixin class that simplifies writing DDK drivers that
// implement the SCPI protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SCPI device.
// class ScpiDevice;
// using ScpiDeviceType = ddk::Device<ScpiDevice, /* ddk mixins */>;
//
// class ScpiDevice : public ScpiDeviceType,
//                    public ddk::ScpiProtocol<ScpiDevice> {
//   public:
//     ScpiDevice(zx_device_t* parent)
//       : ScpiDeviceType("my-scpi-device", parent) {}
//
//    zx_status_t ScpiGetSensor(const char* name, uint32_t* sensor_id);
//    zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value);
//    zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps);
//    zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx);
//    zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx);
//     ...
// };

namespace ddk {

template <typename D>
class ScpiProtocol {
public:
    ScpiProtocol() {
        internal::CheckScpiProtocolSubclass<D>();
        scpi_proto_ops_.get_sensor = ScpiGetSensor;
        scpi_proto_ops_.get_sensor_value = ScpiGetSensorValue;
        scpi_proto_ops_.get_dvfs_info = ScpiGetDvfsInfo;
        scpi_proto_ops_.get_dvfs_idx = ScpiGetDvfsIdx;
        scpi_proto_ops_.set_dvfs_idx = ScpiSetDvfsIdx;
    }

protected:
    scpi_protocol_ops_t scpi_proto_ops_ = {};

private:
    static zx_status_t ScpiGetSensor(void* ctx, const char* name, uint32_t* sensor_id) {
        return static_cast<D*>(ctx)->ScpiGetSensor(name, sensor_id);
    }
    static zx_status_t ScpiGetSensorValue(void* ctx, uint32_t sensor_id, uint32_t* sensor_value) {
        return static_cast<D*>(ctx)->ScpiGetSensorValue(sensor_id, sensor_value);
    }
    static zx_status_t ScpiGetDvfsInfo(void* ctx, uint8_t power_domain, scpi_opp_t* opps) {
        return static_cast<D*>(ctx)->ScpiGetDvfsInfo(power_domain, opps);
    }
    static zx_status_t ScpiGetDvfsIdx(void* ctx, uint8_t power_domain, uint16_t* idx) {
        return static_cast<D*>(ctx)->ScpiGetDvfsIdx(power_domain, idx);
    }
    static zx_status_t ScpiSetDvfsIdx(void* ctx, uint8_t power_domain, uint16_t idx) {
        return static_cast<D*>(ctx)->ScpiSetDvfsIdx(power_domain, idx);
    }
};

class ScpiProtocolProxy {
public:
    ScpiProtocolProxy(scpi_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t ScpiGetSensor(const char* name, uint32_t* sensor_id) {
        return ops_->get_sensor(ctx_, name, sensor_id);
    }
    zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value) {
        return ops_->get_sensor_value(ctx_, sensor_id, sensor_value);
    }
    zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps) {
        return ops_->get_dvfs_info(ctx_, power_domain, opps);
    }
    zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx) {
        return ops_->get_dvfs_idx(ctx_, power_domain, idx);
    }
    zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx) {
        return ops_->set_dvfs_idx(ctx_, power_domain, idx);
    }
\
private:
    scpi_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
