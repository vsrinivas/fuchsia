// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.passcallback banjo file

#pragma once

#include <banjo/examples/passcallback/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK passcallback-protocol support
//
// :: Proxies ::
//
// ddk::ActionProtocolProtocolClient is a simple wrapper around
// action_protocol_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ActionProtocolProtocol is a mixin class that simplifies writing DDK drivers
// that implement the action-protocol protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ACTION_PROTOCOL device.
// class ActionProtocolDevice;
// using ActionProtocolDeviceType = ddk::Device<ActionProtocolDevice, /* ddk mixins */>;
//
// class ActionProtocolDevice : public ActionProtocolDeviceType,
//                      public ddk::ActionProtocolProtocol<ActionProtocolDevice> {
//   public:
//     ActionProtocolDevice(zx_device_t* parent)
//         : ActionProtocolDeviceType(parent) {}
//
//     zx_status_t ActionProtocolRegisterCallback(uint32_t id, const action_notify_t* cb);
//
//     zx_status_t ActionProtocolGetCallback(uint32_t id, action_notify_t* out_cb);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ActionProtocolProtocol : public Base {
public:
    ActionProtocolProtocol() {
        internal::CheckActionProtocolProtocolSubclass<D>();
        action_protocol_protocol_ops_.register_callback = ActionProtocolRegisterCallback;
        action_protocol_protocol_ops_.get_callback = ActionProtocolGetCallback;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ACTION_PROTOCOL;
            dev->ddk_proto_ops_ = &action_protocol_protocol_ops_;
        }
    }

protected:
    action_protocol_protocol_ops_t action_protocol_protocol_ops_ = {};

private:
    static zx_status_t ActionProtocolRegisterCallback(void* ctx, uint32_t id, const action_notify_t* cb) {
        auto ret = static_cast<D*>(ctx)->ActionProtocolRegisterCallback(id, cb);
        return ret;
    }
    static zx_status_t ActionProtocolGetCallback(void* ctx, uint32_t id, action_notify_t* out_cb) {
        auto ret = static_cast<D*>(ctx)->ActionProtocolGetCallback(id, out_cb);
        return ret;
    }
};

class ActionProtocolProtocolClient {
public:
    ActionProtocolProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ActionProtocolProtocolClient(const action_protocol_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ActionProtocolProtocolClient(zx_device_t* parent) {
        action_protocol_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ACTION_PROTOCOL, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    ActionProtocolProtocolClient(zx_device_t* parent, const char* fragment_name) {
        action_protocol_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ACTION_PROTOCOL, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ActionProtocolProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        ActionProtocolProtocolClient* result) {
        action_protocol_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ACTION_PROTOCOL, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ActionProtocolProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a ActionProtocolProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        ActionProtocolProtocolClient* result) {
        action_protocol_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ACTION_PROTOCOL, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ActionProtocolProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(action_protocol_protocol_t* proto) const {
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

    zx_status_t RegisterCallback(uint32_t id, const action_notify_t* cb) const {
        return ops_->register_callback(ctx_, id, cb);
    }

    zx_status_t GetCallback(uint32_t id, action_notify_t* out_cb) const {
        return ops_->get_callback(ctx_, id, out_cb);
    }

private:
    const action_protocol_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
