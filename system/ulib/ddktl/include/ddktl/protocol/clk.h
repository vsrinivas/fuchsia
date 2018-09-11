// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/clk.banjo INSTEAD.

#pragma once

#include <ddk/protocol/clk.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "clk-internal.h"

// DDK clk-protocol support
//
// :: Proxies ::
//
// ddk::ClkProtocolProxy is a simple wrapper around
// clk_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::ClkProtocol is a mixin class that simplifies writing DDK drivers
// that implement the clk protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_CLK device.
// class ClkDevice {
// using ClkDeviceType = ddk::Device<ClkDevice, /* ddk mixins */>;
//
// class ClkDevice : public ClkDeviceType,
//                   public ddk::ClkProtocol<ClkDevice> {
//   public:
//     ClkDevice(zx_device_t* parent)
//         : ClkDeviceType("my-clk-protocol-device", parent) {}
//
//     zx_status_t ClkEnable(uint32_t index);
//
//     zx_status_t ClkDisable(uint32_t index);
//
//     ...
// };

namespace ddk {

template <typename D>
class ClkProtocol : public internal::base_protocol {
public:
    ClkProtocol() {
        internal::CheckClkProtocolSubclass<D>();
        ops_.enable = ClkEnable;
        ops_.disable = ClkDisable;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_CLK;
        ddk_proto_ops_ = &ops_;
    }

protected:
    clk_protocol_ops_t ops_ = {};

private:
    static zx_status_t ClkEnable(void* ctx, uint32_t index) {
        return static_cast<D*>(ctx)->ClkEnable(index);
    }
    static zx_status_t ClkDisable(void* ctx, uint32_t index) {
        return static_cast<D*>(ctx)->ClkDisable(index);
    }
};

class ClkProtocolProxy {
public:
    ClkProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    ClkProtocolProxy(const clk_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(clk_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t Enable(uint32_t index) { return ops_->enable(ctx_, index); }
    zx_status_t Disable(uint32_t index) { return ops_->disable(ctx_, index); }

private:
    clk_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
