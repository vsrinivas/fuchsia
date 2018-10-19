// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-proxy.h>
#include <ddktl/device.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>

#include "platform-proxy-device.h"
#include "proxy-protocol.h"

namespace platform_bus {

class ProxyDevice;

class PlatformProxy;
using PlatformProxyType = ddk::Device<PlatformProxy>;

// This is the main class for the proxy side platform bus driver.
// It handles RPC communication with the main platform bus driver in the root devhost.
// It also manages a collection of protocol clients for vendor or SOC-specific protocols.
// In the case where these protocols exist, this class loads the protocol client drivers
// before starting the platform device driver.
class PlatformProxy : public PlatformProxyType, public fbl::RefCounted<PlatformProxy> {
public:
    static zx_status_t Create(zx_device_t* parent, zx_handle_t rpc_channel);

    // Device protocol implementation.
    void DdkRelease();

    zx_status_t Rpc(uint32_t device_id, platform_proxy_req_t* req, uint32_t req_length,
                    platform_proxy_rsp_t* resp, uint32_t resp_length,
                    zx_handle_t* in_handles, uint32_t in_handle_count,
                    zx_handle_t* out_handles, uint32_t out_handle_count,
                    size_t* out_actual);

    inline zx_status_t Rpc(uint32_t device_id, platform_proxy_req_t* req, uint32_t req_length,
                           platform_proxy_rsp_t* resp, uint32_t resp_length) {
        return Rpc(device_id, req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
    }

    zx_status_t GetProtocol(uint32_t proto_id, void* out);
    zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol);
    void UnregisterProtocol(uint32_t proto_id);
    zx_status_t Proxy(platform_proxy_args_t* args);

private:
    // This class is a wrapper for a protocol added via platform_proxy_register_protocol().
    // It also is the element type for the protocols_ WAVL tree.
    class PlatformProtocol : public fbl::WAVLTreeContainable<fbl::unique_ptr<PlatformProtocol>> {
    public:
        PlatformProtocol(uint32_t proto_id, const ddk::AnyProtocol* protocol)
            : proto_id_(proto_id), protocol_(*protocol) {}

        inline uint32_t GetKey() const { return proto_id_; }
        inline void GetProtocol(void* out) const { memcpy(out, &protocol_, sizeof(protocol_)); }

    private:
        const uint32_t proto_id_;
        ddk::AnyProtocol protocol_;
    };

    friend class fbl::RefPtr<PlatformProxy>;
    friend class fbl::internal::MakeRefCountedHelper<PlatformProxy>;

    explicit PlatformProxy(zx_device_t* parent, zx_handle_t rpc_channel)
        :  PlatformProxyType(parent), rpc_channel_(rpc_channel) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformProxy);

    zx_status_t Init(zx_device_t* parent);

    const zx::channel rpc_channel_;
    fbl::WAVLTree<uint32_t, fbl::unique_ptr<PlatformProtocol>> protocols_;
    uint32_t protocol_count_;
};

} // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel);
__END_CDECLS
