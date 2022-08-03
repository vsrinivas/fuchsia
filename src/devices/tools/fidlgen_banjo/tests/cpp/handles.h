// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.handles banjo file

#pragma once

#include <banjo/examples/handles/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK handles-protocol support
//
// :: Proxies ::
//
// ddk::DoerProtocolClient is a simple wrapper around
// doer_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::DoerProtocol is a mixin class that simplifies writing DDK drivers
// that implement the doer protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_DOER device.
// class DoerDevice;
// using DoerDeviceType = ddk::Device<DoerDevice, /* ddk mixins */>;
//
// class DoerDevice : public DoerDeviceType,
//                      public ddk::DoerProtocol<DoerDevice> {
//   public:
//     DoerDevice(zx_device_t* parent)
//         : DoerDeviceType(parent) {}
//
//     void DoerDoSomething(zx::channel the_handle);
//
//     void DoerDoSomethingElse(zx::channel the_handle_too);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class DoerProtocol : public Base {
public:
    DoerProtocol() {
        internal::CheckDoerProtocolSubclass<D>();
        doer_protocol_ops_.do_something = DoerDoSomething;
        doer_protocol_ops_.do_something_else = DoerDoSomethingElse;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_DOER;
            dev->ddk_proto_ops_ = &doer_protocol_ops_;
        }
    }

protected:
    doer_protocol_ops_t doer_protocol_ops_ = {};

private:
    static void DoerDoSomething(void* ctx, zx_handle_t the_handle) {
        static_cast<D*>(ctx)->DoerDoSomething(zx::channel(the_handle));
    }
    static void DoerDoSomethingElse(void* ctx, zx_handle_t the_handle_too) {
        static_cast<D*>(ctx)->DoerDoSomethingElse(zx::channel(the_handle_too));
    }
};

class DoerProtocolClient {
public:
    DoerProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    DoerProtocolClient(const doer_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    DoerProtocolClient(zx_device_t* parent) {
        doer_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_DOER, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    DoerProtocolClient(zx_device_t* parent, const char* fragment_name) {
        doer_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_DOER, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a DoerProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        DoerProtocolClient* result) {
        doer_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_DOER, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = DoerProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a DoerProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        DoerProtocolClient* result) {
        doer_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_DOER, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = DoerProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(doer_protocol_t* proto) const {
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

    void DoSomething(zx::channel the_handle) const {
        ops_->do_something(ctx_, the_handle.release());
    }

    void DoSomethingElse(zx::channel the_handle_too) const {
        ops_->do_something_else(ctx_, the_handle_too.release());
    }

private:
    const doer_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
