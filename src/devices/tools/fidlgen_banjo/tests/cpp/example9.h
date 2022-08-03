// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example9 banjo file

#pragma once

#include <banjo/examples/example9/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK example9-protocol support
//
// :: Proxies ::
//
// ddk::EchoProtocolClient is a simple wrapper around
// echo_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::EchoProtocol is a mixin class that simplifies writing DDK drivers
// that implement the echo protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ECHO device.
// class EchoDevice;
// using EchoDeviceType = ddk::Device<EchoDevice, /* ddk mixins */>;
//
// class EchoDevice : public EchoDeviceType,
//                      public ddk::EchoProtocol<EchoDevice> {
//   public:
//     EchoDevice(zx_device_t* parent)
//         : EchoDeviceType(parent) {}
//
//     uint32_t EchoEcho32(uint32_t uint32);
//
//     uint64_t EchoEcho64(uint64_t uint64);
//
//     echo_me_t EchoEchoEnum(echo_me_t req);
//
//     void EchoEchoHandle(zx::handle req, zx::handle* out_response);
//
//     void EchoEchoChannel(zx::channel req, zx::channel* out_response);
//
//     void EchoEchoStruct(const echo_more_t* req, echo_more_t* out_response);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class EchoProtocol : public Base {
public:
    EchoProtocol() {
        internal::CheckEchoProtocolSubclass<D>();
        echo_protocol_ops_.echo32 = EchoEcho32;
        echo_protocol_ops_.echo64 = EchoEcho64;
        echo_protocol_ops_.echo_enum = EchoEchoEnum;
        echo_protocol_ops_.echo_handle = EchoEchoHandle;
        echo_protocol_ops_.echo_channel = EchoEchoChannel;
        echo_protocol_ops_.echo_struct = EchoEchoStruct;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ECHO;
            dev->ddk_proto_ops_ = &echo_protocol_ops_;
        }
    }

protected:
    echo_protocol_ops_t echo_protocol_ops_ = {};

private:
    static uint32_t EchoEcho32(void* ctx, uint32_t uint32) {
        auto ret = static_cast<D*>(ctx)->EchoEcho32(uint32);
        return ret;
    }
    static uint64_t EchoEcho64(void* ctx, uint64_t uint64) {
        auto ret = static_cast<D*>(ctx)->EchoEcho64(uint64);
        return ret;
    }
    static echo_me_t EchoEchoEnum(void* ctx, echo_me_t req) {
        auto ret = static_cast<D*>(ctx)->EchoEchoEnum(req);
        return ret;
    }
    static void EchoEchoHandle(void* ctx, zx_handle_t req, zx_handle_t* out_response) {
        zx::handle out_response2;
        static_cast<D*>(ctx)->EchoEchoHandle(zx::handle(req), &out_response2);
        *out_response = out_response2.release();
    }
    static void EchoEchoChannel(void* ctx, zx_handle_t req, zx_handle_t* out_response) {
        zx::channel out_response2;
        static_cast<D*>(ctx)->EchoEchoChannel(zx::channel(req), &out_response2);
        *out_response = out_response2.release();
    }
    static void EchoEchoStruct(void* ctx, const echo_more_t* req, echo_more_t* out_response) {
        static_cast<D*>(ctx)->EchoEchoStruct(req, out_response);
    }
};

class EchoProtocolClient {
public:
    EchoProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    EchoProtocolClient(const echo_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    EchoProtocolClient(zx_device_t* parent) {
        echo_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ECHO, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    EchoProtocolClient(zx_device_t* parent, const char* fragment_name) {
        echo_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ECHO, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a EchoProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        EchoProtocolClient* result) {
        echo_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ECHO, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = EchoProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a EchoProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        EchoProtocolClient* result) {
        echo_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ECHO, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = EchoProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(echo_protocol_t* proto) const {
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

    uint32_t Echo32(uint32_t uint32) const {
        return ops_->echo32(ctx_, uint32);
    }

    uint64_t Echo64(uint64_t uint64) const {
        return ops_->echo64(ctx_, uint64);
    }

    echo_me_t EchoEnum(echo_me_t req) const {
        return ops_->echo_enum(ctx_, req);
    }

    void EchoHandle(zx::handle req, zx::handle* out_response) const {
        ops_->echo_handle(ctx_, req.release(), out_response->reset_and_get_address());
    }

    void EchoChannel(zx::channel req, zx::channel* out_response) const {
        ops_->echo_channel(ctx_, req.release(), out_response->reset_and_get_address());
    }

    void EchoStruct(const echo_more_t* req, echo_more_t* out_response) const {
        ops_->echo_struct(ctx_, req, out_response);
    }

private:
    const echo_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
