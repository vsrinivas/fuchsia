// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_bus.banjo INSTEAD.

#pragma once

#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "platform-bus-internal.h"

// DDK pbus-protocol support
//
// :: Proxies ::
//
// ddk::PBusProtocolProxy is a simple wrapper around
// pbus_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::PBusProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pbus protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PBUS device.
// class PBusDevice {
// using PBusDeviceType = ddk::Device<PBusDevice, /* ddk mixins */>;
//
// class PBusDevice : public PBusDeviceType,
//                    public ddk::PBusProtocol<PBusDevice> {
//   public:
//     PBusDevice(zx_device_t* parent)
//         : PBusDeviceType("my-pbus-protocol-device", parent) {}
//
//     zx_status_t PBusDeviceAdd(const pbus_dev_t* dev);
//
//     zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);
//
//     zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* protocol_buffer, size_t
//     protocol_size, const platform_proxy_cb_t* proxy_cb);
//
//     zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info);
//
//     zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info);
//
//     ...
// };

namespace ddk {

template <typename D>
class PBusProtocol : public internal::base_protocol {
public:
    PBusProtocol() {
        internal::CheckPBusProtocolSubclass<D>();
        ops_.device_add = PBusDeviceAdd;
        ops_.protocol_device_add = PBusProtocolDeviceAdd;
        ops_.register_protocol = PBusRegisterProtocol;
        ops_.get_board_info = PBusGetBoardInfo;
        ops_.set_board_info = PBusSetBoardInfo;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_PBUS;
        ddk_proto_ops_ = &ops_;
    }

protected:
    pbus_protocol_ops_t ops_ = {};

private:
    // Adds a new platform device to the bus, using configuration provided by |dev|.
    // Platform devices are created in their own separate devhosts.
    static zx_status_t PBusDeviceAdd(void* ctx, const pbus_dev_t* dev) {
        return static_cast<D*>(ctx)->PBusDeviceAdd(dev);
    }
    // Adds a device for binding a protocol implementation driver.
    // These devices are added in the same devhost as the platform bus.
    // After the driver binds to the device it calls `pbus_register_protocol()`
    // to register its protocol with the platform bus.
    // `pbus_protocol_device_add()` blocks until the protocol implementation driver
    // registers its protocol (or times out).
    static zx_status_t PBusProtocolDeviceAdd(void* ctx, uint32_t proto_id, const pbus_dev_t* dev) {
        return static_cast<D*>(ctx)->PBusProtocolDeviceAdd(proto_id, dev);
    }
    // Called by protocol implementation drivers to register their protocol
    // with the platform bus.
    static zx_status_t PBusRegisterProtocol(void* ctx, uint32_t proto_id,
                                            const void* protocol_buffer, size_t protocol_size,
                                            const platform_proxy_cb_t* proxy_cb) {
        return static_cast<D*>(ctx)->PBusRegisterProtocol(proto_id, protocol_buffer, protocol_size,
                                                          proxy_cb);
    }
    // Board drivers may use this to get information about the board, and to
    // differentiate between multiple boards that they support.
    static zx_status_t PBusGetBoardInfo(void* ctx, pdev_board_info_t* out_info) {
        return static_cast<D*>(ctx)->PBusGetBoardInfo(out_info);
    }
    // Board drivers may use this to set information about the board
    // (like the board revision number).
    // Platform device drivers can access this via `pdev_get_board_info()`.
    static zx_status_t PBusSetBoardInfo(void* ctx, const pbus_board_info_t* info) {
        return static_cast<D*>(ctx)->PBusSetBoardInfo(info);
    }
};

class PBusProtocolProxy {
public:
    PBusProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    PBusProtocolProxy(const pbus_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(pbus_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Adds a new platform device to the bus, using configuration provided by |dev|.
    // Platform devices are created in their own separate devhosts.
    zx_status_t DeviceAdd(const pbus_dev_t* dev) { return ops_->device_add(ctx_, dev); }
    // Adds a device for binding a protocol implementation driver.
    // These devices are added in the same devhost as the platform bus.
    // After the driver binds to the device it calls `pbus_register_protocol()`
    // to register its protocol with the platform bus.
    // `pbus_protocol_device_add()` blocks until the protocol implementation driver
    // registers its protocol (or times out).
    zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
        return ops_->protocol_device_add(ctx_, proto_id, dev);
    }
    // Called by protocol implementation drivers to register their protocol
    // with the platform bus.
    zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol_buffer,
                                 size_t protocol_size, const platform_proxy_cb_t* proxy_cb) {
        return ops_->register_protocol(ctx_, proto_id, protocol_buffer, protocol_size, proxy_cb);
    }
    // Board drivers may use this to get information about the board, and to
    // differentiate between multiple boards that they support.
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info) {
        return ops_->get_board_info(ctx_, out_info);
    }
    // Board drivers may use this to set information about the board
    // (like the board revision number).
    // Platform device drivers can access this via `pdev_get_board_info()`.
    zx_status_t SetBoardInfo(const pbus_board_info_t* info) {
        return ops_->set_board_info(ctx_, info);
    }

private:
    pbus_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
