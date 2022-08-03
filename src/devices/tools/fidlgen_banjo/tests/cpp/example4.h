// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example4 banjo file

#pragma once

#include <banjo/examples/example4/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK example4-protocol support
//
// :: Proxies ::
//
// ddk::InterfaceProtocolClient is a simple wrapper around
// interface_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::InterfaceProtocol is a mixin class that simplifies writing DDK drivers
// that implement the interface protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_INTERFACE device.
// class InterfaceDevice;
// using InterfaceDeviceType = ddk::Device<InterfaceDevice, /* ddk mixins */>;
//
// class InterfaceDevice : public InterfaceDeviceType,
//                      public ddk::InterfaceProtocol<InterfaceDevice> {
//   public:
//     InterfaceDevice(zx_device_t* parent)
//         : InterfaceDeviceType(parent) {}
//
//     void Interfacefunc(bool x);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class InterfaceProtocol : public Base {
public:
    InterfaceProtocol() {
        internal::CheckInterfaceProtocolSubclass<D>();
        interface_protocol_ops_.func = Interfacefunc;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_INTERFACE;
            dev->ddk_proto_ops_ = &interface_protocol_ops_;
        }
    }

protected:
    interface_protocol_ops_t interface_protocol_ops_ = {};

private:
    static void Interfacefunc(void* ctx, bool x) {
        static_cast<D*>(ctx)->Interfacefunc(x);
    }
};

class InterfaceProtocolClient {
public:
    InterfaceProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    InterfaceProtocolClient(const interface_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    InterfaceProtocolClient(zx_device_t* parent) {
        interface_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_INTERFACE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    InterfaceProtocolClient(zx_device_t* parent, const char* fragment_name) {
        interface_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_INTERFACE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a InterfaceProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        InterfaceProtocolClient* result) {
        interface_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_INTERFACE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InterfaceProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a InterfaceProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        InterfaceProtocolClient* result) {
        interface_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_INTERFACE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InterfaceProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(interface_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void func(bool x) const {
        ops_->func(ctx_, x);
    }

private:
    const interface_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
