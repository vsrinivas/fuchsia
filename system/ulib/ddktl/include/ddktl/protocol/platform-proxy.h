// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_proxy.banjo INSTEAD.

#pragma once

#include <ddk/protocol/platform-proxy.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "platform-proxy-internal.h"

// DDK platform-proxy-protocol support
//
// :: Proxies ::
//
// ddk::PlatformProxyProtocolProxy is a simple wrapper around
// platform_proxy_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::PlatformProxyProtocol is a mixin class that simplifies writing DDK drivers
// that implement the platform-proxy protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PLATFORM_PROXY device.
// class PlatformProxyDevice {
// using PlatformProxyDeviceType = ddk::Device<PlatformProxyDevice, /* ddk mixins */>;
//
// class PlatformProxyDevice : public PlatformProxyDeviceType,
//                             public ddk::PlatformProxyProtocol<PlatformProxyDevice> {
//   public:
//     PlatformProxyDevice(zx_device_t* parent)
//         : PlatformProxyDeviceType("my-platform-proxy-protocol-device", parent) {}
//
//     zx_status_t PlatformProxyRegisterProtocol(uint32_t proto_id, const void* protocol_buffer,
//     size_t protocol_size);
//
//     zx_status_t PlatformProxyProxy(const void* req_buffer, size_t req_size, const zx_handle_t*
//     req_handle_list, size_t req_handle_count, void* out_resp_buffer, size_t resp_size, size_t*
//     out_resp_actual, zx_handle_t* out_resp_handle_list, size_t resp_handle_count, size_t*
//     out_resp_handle_actual);
//
//     ...
// };

namespace ddk {

template <typename D>
class PlatformProxyProtocol : public internal::base_protocol {
public:
    PlatformProxyProtocol() {
        internal::CheckPlatformProxyProtocolSubclass<D>();
        ops_.register_protocol = PlatformProxyRegisterProtocol;
        ops_.proxy = PlatformProxyProxy;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_PLATFORM_PROXY;
        ddk_proto_ops_ = &ops_;
    }

protected:
    platform_proxy_protocol_ops_t ops_ = {};

private:
    // Used by protocol client drivers to register their local protocol implementation
    // with the platform proxy driver.
    static zx_status_t PlatformProxyRegisterProtocol(void* ctx, uint32_t proto_id,
                                                     const void* protocol_buffer,
                                                     size_t protocol_size) {
        return static_cast<D*>(ctx)->PlatformProxyRegisterProtocol(proto_id, protocol_buffer,
                                                                   protocol_size);
    }
    // Used by protocol client drivers to proxy a protocol call to the protocol implementation
    // driver in the platform bus driver's devhost.
    static zx_status_t PlatformProxyProxy(void* ctx, const void* req_buffer, size_t req_size,
                                          const zx_handle_t* req_handle_list,
                                          size_t req_handle_count, void* out_resp_buffer,
                                          size_t resp_size, size_t* out_resp_actual,
                                          zx_handle_t* out_resp_handle_list,
                                          size_t resp_handle_count,
                                          size_t* out_resp_handle_actual) {
        return static_cast<D*>(ctx)->PlatformProxyProxy(
            req_buffer, req_size, req_handle_list, req_handle_count, out_resp_buffer, resp_size,
            out_resp_actual, out_resp_handle_list, resp_handle_count, out_resp_handle_actual);
    }
};

class PlatformProxyProtocolProxy {
public:
    PlatformProxyProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    PlatformProxyProtocolProxy(const platform_proxy_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(platform_proxy_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Used by protocol client drivers to register their local protocol implementation
    // with the platform proxy driver.
    zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol_buffer,
                                 size_t protocol_size) {
        return ops_->register_protocol(ctx_, proto_id, protocol_buffer, protocol_size);
    }
    // Used by protocol client drivers to proxy a protocol call to the protocol implementation
    // driver in the platform bus driver's devhost.
    zx_status_t Proxy(const void* req_buffer, size_t req_size, const zx_handle_t* req_handle_list,
                      size_t req_handle_count, void* out_resp_buffer, size_t resp_size,
                      size_t* out_resp_actual, zx_handle_t* out_resp_handle_list,
                      size_t resp_handle_count, size_t* out_resp_handle_actual) {
        return ops_->proxy(ctx_, req_buffer, req_size, req_handle_list, req_handle_count,
                           out_resp_buffer, resp_size, out_resp_actual, out_resp_handle_list,
                           resp_handle_count, out_resp_handle_actual);
    }

private:
    platform_proxy_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
