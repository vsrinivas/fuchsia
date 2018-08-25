// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-proxy.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "platform-proxy-internal.h"

// DDK platform proxy protocol support.
//
// :: Proxies ::
//
// ddk::PlatformProxyProtocolProxy is a simple wrappers around platform_proxy_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::PlatformProxyProtocol is a mixin class that simplifies writing DDK drivers that
// implement the platform proxy protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PLATFORM_PROXY device.
// class PlatformProxyDevice;
// using PlatformProxyDeviceType = ddk::Device<PlatformProxyDevice, /* ddk mixins */>;
//
// class PlatformProxyDevice : public PlatformProxyDeviceType,
//                             public ddk::PlatformProxyProtocol<PlatformProxyDevice> {
//   public:
//     PlatformProxyDevice(zx_device_t* parent)
//       : PlatformProxyDeviceType("my-platform-proxy", parent) {}
//
//     zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol);
//     zx_status_t Proxy(platform_proxy_args_t* args);
//     ...
// };

namespace ddk {

template <typename D>
class PlatformProxyProtocol : public internal::base_protocol {
public:
    PlatformProxyProtocol() {
        internal::CheckPlatformProxyProtocolSubclass<D>();
        platform_proxy_proto_ops_.register_protocol = RegisterProtocol;
        platform_proxy_proto_ops_.proxy = Proxy;

       // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_PLATFORM_PROXY;
        ddk_proto_ops_ = &platform_proxy_proto_ops_;
    }

protected:
    platform_proxy_protocol_ops_t platform_proxy_proto_ops_ = {};

private:
    static zx_status_t RegisterProtocol(void* ctx, uint32_t proto_id, const void* protocol) {
        return static_cast<D*>(ctx)->RegisterProtocol(proto_id, protocol);
    }

    static zx_status_t Proxy(void* ctx, platform_proxy_args_t* args) {
        return static_cast<D*>(ctx)->Proxy(args);
    }
};

class PlatformProxyProtocolProxy {
public:
    PlatformProxyProtocolProxy(platform_proxy_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol) {
        return ops_->register_protocol(ctx_, proto_id, protocol);
    }

    zx_status_t Proxy(platform_proxy_args_t* args) {
        return ops_->proxy(ctx_, args);
    }

private:
    platform_proxy_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
