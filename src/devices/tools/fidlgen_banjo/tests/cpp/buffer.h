// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.buffer banjo file

#pragma once

#include <banjo/examples/buffer/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK buffer-protocol support
//
// :: Proxies ::
//
// ddk::SomeMethodsProtocolClient is a simple wrapper around
// some_methods_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SomeMethodsProtocol is a mixin class that simplifies writing DDK drivers
// that implement the some-methods protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SOME_METHODS device.
// class SomeMethodsDevice;
// using SomeMethodsDeviceType = ddk::Device<SomeMethodsDevice, /* ddk mixins */>;
//
// class SomeMethodsDevice : public SomeMethodsDeviceType,
//                      public ddk::SomeMethodsProtocol<SomeMethodsDevice> {
//   public:
//     SomeMethodsDevice(zx_device_t* parent)
//         : SomeMethodsDeviceType(parent) {}
//
//     void SomeMethodsDoSomething(const uint8_t* input_buffer, size_t input_size);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SomeMethodsProtocol : public Base {
public:
    SomeMethodsProtocol() {
        internal::CheckSomeMethodsProtocolSubclass<D>();
        some_methods_protocol_ops_.do_something = SomeMethodsDoSomething;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SOME_METHODS;
            dev->ddk_proto_ops_ = &some_methods_protocol_ops_;
        }
    }

protected:
    some_methods_protocol_ops_t some_methods_protocol_ops_ = {};

private:
    static void SomeMethodsDoSomething(void* ctx, const uint8_t* input_buffer, size_t input_size) {
        static_cast<D*>(ctx)->SomeMethodsDoSomething(input_buffer, input_size);
    }
};

class SomeMethodsProtocolClient {
public:
    SomeMethodsProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SomeMethodsProtocolClient(const some_methods_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SomeMethodsProtocolClient(zx_device_t* parent) {
        some_methods_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SOME_METHODS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    SomeMethodsProtocolClient(zx_device_t* parent, const char* fragment_name) {
        some_methods_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_SOME_METHODS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a SomeMethodsProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        SomeMethodsProtocolClient* result) {
        some_methods_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_SOME_METHODS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SomeMethodsProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a SomeMethodsProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        SomeMethodsProtocolClient* result) {
        some_methods_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_SOME_METHODS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SomeMethodsProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(some_methods_protocol_t* proto) const {
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

    void DoSomething(const uint8_t* input_buffer, size_t input_size) const {
        ops_->do_something(ctx_, input_buffer, input_size);
    }

private:
    const some_methods_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
