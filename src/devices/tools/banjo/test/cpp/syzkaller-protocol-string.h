// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.string banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/string.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "string-internal.h"

// DDK string-protocol support
//
// :: Proxies ::
//
// ddk::ApiProtocolClient is a simple wrapper around
// api_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ApiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the api protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_API device.
// class ApiDevice;
// using ApiDeviceType = ddk::Device<ApiDevice, /* ddk mixins */>;
//
// class ApiDevice : public ApiDeviceType,
//                      public ddk::ApiProtocol<ApiDevice> {
//   public:
//     ApiDevice(zx_device_t* parent)
//         : ApiDeviceType(parent) {}
//
//     zx_status_t ApiString(const char* str, size_t str_len);
//
//     zx_status_t ApiString(const char* str, size_t str_len);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ApiProtocol : public Base {
public:
    ApiProtocol() {
        internal::CheckApiProtocolSubclass<D>();
        api_protocol_ops_.string = ApiString;
        api_protocol_ops_.string = ApiString;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_API;
            dev->ddk_proto_ops_ = &api_protocol_ops_;
        }
    }

protected:
    api_protocol_ops_t api_protocol_ops_ = {};

private:
    static zx_status_t ApiString(void* ctx, const char* str, size_t str_len) {
        auto ret = static_cast<D*>(ctx)->ApiString(str, str_len);
        return ret;
    }
    static zx_status_t ApiString(void* ctx, const char* str, size_t str_len) {
        auto ret = static_cast<D*>(ctx)->ApiString(str, str_len);
        return ret;
    }
};

class ApiProtocolClient {
public:
    ApiProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ApiProtocolClient(const api_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ApiProtocolClient(zx_device_t* parent) {
        api_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_API, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ApiProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        ApiProtocolClient* result) {
        api_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_API, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ApiProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(api_protocol_t* proto) const {
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

    zx_status_t String(const char* str, size_t str_len) const {
        return ops_->string(ctx_, str, str_len);
    }

    zx_status_t String(const char* str, size_t str_len) const {
        return ops_->string(ctx_, str, str_len);
    }

private:
    api_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
