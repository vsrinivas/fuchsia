// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/platform-bus.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "platform-bus-internal.h"

// DDK platform bus protocol support.
//
// :: Proxies ::
//
// ddk::PlatformBusProtocolProxy is a simple wrappers around platform_bus_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::PlatformBusProtocol is a mixin class that simplifies writing DDK drivers that
// implement the platform bus protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PLATFORM_BUS device.
// class PlatformBusDevice;
// using PlatformBusDeviceType = ddk::Device<PlatformBusDevice, /* ddk mixins */>;
//
// class PlatformBusDevice : public PlatformBusDeviceType,
//                           public ddk::PlatformBusProtocol<PlatformBusDevice> {
//   public:
//     PlatformBusDevice(zx_device_t* parent)
//       : PlatformBusDeviceType("my-platform-bus", parent) {}
//
//    zx_status_t DeviceAdd(const pbus_dev_t* dev);
//    zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);
//    zx_status_t RegisterProtocol(uint32_t proto_id, void* protocol, platform_proxy_cb_t proxy_cb,
//                                 void* proxy_cb_cookie);
//    const char* GetBoardName();
//    zx_status_t SetBoardInfo(const pbus_board_info_t* info);
//     ...
// };

namespace ddk {

template <typename D>
class PlatformBusProtocol : public internal::base_protocol {
public:
    PlatformBusProtocol() {
        internal::CheckPlatformBusProtocolSubclass<D>();
        pbus_proto_ops_.device_add = DeviceAdd;
        pbus_proto_ops_.protocol_device_add = ProtocolDeviceAdd;
        pbus_proto_ops_.register_protocol = RegisterProtocol;
        pbus_proto_ops_.get_board_name = GetBoardName;
        pbus_proto_ops_.set_board_info = SetBoardInfo;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_PLATFORM_BUS;
        ddk_proto_ops_ = &pbus_proto_ops_;
    }

protected:
    platform_bus_protocol_ops_t pbus_proto_ops_ = {};

private:
    static zx_status_t DeviceAdd(void* ctx, const pbus_dev_t* dev) {
        return static_cast<D*>(ctx)->DeviceAdd(dev);
    }

    static zx_status_t ProtocolDeviceAdd(void* ctx, uint32_t proto_id, const pbus_dev_t* dev) {
        return static_cast<D*>(ctx)->ProtocolDeviceAdd(proto_id, dev);
    }

    static zx_status_t RegisterProtocol(void* ctx, uint32_t proto_id, void* protocol,
                                        platform_proxy_cb_t proxy_cb, void* proxy_cb_cookie) {
        return static_cast<D*>(ctx)->RegisterProtocol(proto_id, protocol, proxy_cb, proxy_cb_cookie);
    }

    static const char* GetBoardName(void* ctx) {
        return static_cast<D*>(ctx)->GetBoardName();
    }

    static zx_status_t SetBoardInfo(void* ctx, const pbus_board_info_t* info) {
        return static_cast<D*>(ctx)->SetBoardInfo(info);
    }
};

class PlatformBusProtocolProxy {
public:
    PlatformBusProtocolProxy(platform_bus_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t DeviceAdd(const pbus_dev_t* dev) {
        return ops_->device_add(ctx_, dev);
    }

    zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
        return ops_->protocol_device_add(ctx_, proto_id, dev);
    }

    zx_status_t RegisterProtocol(uint32_t proto_id, void* protocol, platform_proxy_cb_t proxy_cb,
                                 void* proxy_cb_cookie) {
        return ops_->register_protocol(ctx_, proto_id, protocol, proxy_cb, proxy_cb_cookie);
    }

    const char* GetBoardName() {
        return ops_->get_board_name(ctx_);
    }

    zx_status_t SetBoardInfo(const pbus_board_info_t* info) {
        return ops_->set_board_info(ctx_, info);
    }

private:
    platform_bus_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
