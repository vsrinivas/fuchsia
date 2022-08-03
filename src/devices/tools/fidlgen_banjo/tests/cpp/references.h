// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.references banjo file

#pragma once

#include <banjo/examples/references/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK references-protocol support
//
// :: Proxies ::
//
// ddk::InOutProtocolProtocolClient is a simple wrapper around
// in_out_protocol_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::InOutProtocolProtocol is a mixin class that simplifies writing DDK drivers
// that implement the in-out-protocol protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_IN_OUT_PROTOCOL device.
// class InOutProtocolDevice;
// using InOutProtocolDeviceType = ddk::Device<InOutProtocolDevice, /* ddk mixins */>;
//
// class InOutProtocolDevice : public InOutProtocolDeviceType,
//                      public ddk::InOutProtocolProtocol<InOutProtocolDevice> {
//   public:
//     InOutProtocolDevice(zx_device_t* parent)
//         : InOutProtocolDeviceType(parent) {}
//
//     void InOutProtocolDoSomething(some_type_t* param);
//
//     void InOutProtocolDoSomeOtherThing(const some_type_t* param);
//
//     void InOutProtocolDoSomeDefaultThing(const some_type_t* param);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class InOutProtocolProtocol : public Base {
public:
    InOutProtocolProtocol() {
        internal::CheckInOutProtocolProtocolSubclass<D>();
        in_out_protocol_protocol_ops_.do_something = InOutProtocolDoSomething;
        in_out_protocol_protocol_ops_.do_some_other_thing = InOutProtocolDoSomeOtherThing;
        in_out_protocol_protocol_ops_.do_some_default_thing = InOutProtocolDoSomeDefaultThing;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_IN_OUT_PROTOCOL;
            dev->ddk_proto_ops_ = &in_out_protocol_protocol_ops_;
        }
    }

protected:
    in_out_protocol_protocol_ops_t in_out_protocol_protocol_ops_ = {};

private:
    static void InOutProtocolDoSomething(void* ctx, some_type_t* param) {
        static_cast<D*>(ctx)->InOutProtocolDoSomething(param);
    }
    static void InOutProtocolDoSomeOtherThing(void* ctx, const some_type_t* param) {
        static_cast<D*>(ctx)->InOutProtocolDoSomeOtherThing(param);
    }
    static void InOutProtocolDoSomeDefaultThing(void* ctx, const some_type_t* param) {
        static_cast<D*>(ctx)->InOutProtocolDoSomeDefaultThing(param);
    }
};

class InOutProtocolProtocolClient {
public:
    InOutProtocolProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    InOutProtocolProtocolClient(const in_out_protocol_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    InOutProtocolProtocolClient(zx_device_t* parent) {
        in_out_protocol_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_IN_OUT_PROTOCOL, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    InOutProtocolProtocolClient(zx_device_t* parent, const char* fragment_name) {
        in_out_protocol_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_IN_OUT_PROTOCOL, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a InOutProtocolProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        InOutProtocolProtocolClient* result) {
        in_out_protocol_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_IN_OUT_PROTOCOL, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InOutProtocolProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a InOutProtocolProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        InOutProtocolProtocolClient* result) {
        in_out_protocol_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_IN_OUT_PROTOCOL, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InOutProtocolProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(in_out_protocol_protocol_t* proto) const {
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

    void DoSomething(some_type_t* param) const {
        ops_->do_something(ctx_, param);
    }

    void DoSomeOtherThing(const some_type_t* param) const {
        ops_->do_some_other_thing(ctx_, param);
    }

    void DoSomeDefaultThing(const some_type_t* param) const {
        ops_->do_some_default_thing(ctx_, param);
    }

private:
    const in_out_protocol_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
