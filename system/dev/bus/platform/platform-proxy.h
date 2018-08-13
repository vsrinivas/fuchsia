// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy : public fbl::RefCounted<PlatformProxy> {
public:
    static zx_status_t Create(zx_device_t* parent, zx_handle_t rpc_channel);

    zx_status_t Rpc(rpc_req_header_t* req, uint32_t req_length, rpc_rsp_header_t* resp,
                    uint32_t resp_length,  zx_handle_t* in_handles, uint32_t in_handle_count,
                    zx_handle_t* out_handles, uint32_t out_handle_count,
                    uint32_t* out_actual);

    inline zx_status_t Rpc(rpc_req_header_t* req, uint32_t req_length, rpc_rsp_header_t* resp,
                           uint32_t resp_length) {
        return Rpc(req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
    }

private:
    friend class fbl::RefPtr<PlatformProxy>;
    friend class fbl::internal::MakeRefCountedHelper<PlatformProxy>;

    explicit PlatformProxy(zx_handle_t rpc_channel)
        : rpc_channel_(rpc_channel) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformProxy);

    zx::channel rpc_channel_;
};

} // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel);
__END_CDECLS
