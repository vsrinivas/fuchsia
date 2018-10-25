// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include "platform-proxy-client.h"
#include "platform-proxy-device.h"

namespace platform_bus {

void PlatformProxy::DdkRelease() {
    // Delete ourselves if the devmgr held the last reference to us.
    if (Release()) {
        delete this;
    }
}

zx_status_t PlatformProxy::Rpc(uint32_t device_id, const platform_proxy_req_t* req,
                               size_t req_length, platform_proxy_rsp_t* resp,
                               size_t resp_length, const zx_handle_t* in_handles,
                               size_t in_handle_count, zx_handle_t* out_handles,
                               size_t out_handle_count, size_t* out_actual) {
    uint32_t resp_size, handle_count;

    // We require the client to pass us the device_id and we set here as a precaution
    // against the code above forgetting to set it.
    const_cast<platform_proxy_req_t*>(req)->device_id = device_id;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = static_cast<uint32_t>(req_length),
        .wr_num_handles = static_cast<uint32_t>(in_handle_count),
        .rd_num_bytes = static_cast<uint32_t>(resp_length),
        .rd_num_handles = static_cast<uint32_t>(out_handle_count),
    };
    auto status = rpc_channel_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
        return status;
    }

    status = resp->status;

    if (status == ZX_OK && resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "PlatformProxy::Rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (status == ZX_OK && handle_count != out_handle_count) {
        zxlogf(ERROR, "PlatformProxy::Rpc handle count %u expected %zu\n", handle_count,
               out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    if (out_actual) {
        *out_actual = resp_size;
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

zx_status_t PlatformProxy::GetProtocol(uint32_t proto_id, void* out) {
    auto protocol = protocols_.find(proto_id);
    if (protocol.IsValid()) {
        protocol->GetProtocol(out);
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PlatformProxy::RegisterProtocol(uint32_t proto_id, const void* protocol) {

    if (protocols_.find(proto_id).IsValid()) {
        zxlogf(ERROR, "%s: protocol %08x has already been registered\n", __func__, proto_id);
        return ZX_ERR_BAD_STATE;
    }

    fbl::AllocChecker ac;
    auto proto = fbl::make_unique_checked<PlatformProtocol>(&ac, proto_id,
                                                static_cast<const ddk::AnyProtocol*>(protocol));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    protocols_.insert(fbl::move(proto));

    if (protocols_.size() == protocol_count_) {
        // All the protocols are registered, so we can now add the actual platform device.
        rpc_pdev_req_t req = {};
        rpc_pdev_rsp_t resp = {};
        req.header.device_id = ROOT_DEVICE_ID;
        req.header.proto_id = ZX_PROTOCOL_PDEV;
        req.header.op = PDEV_GET_DEVICE_INFO;

        auto status = Rpc(ROOT_DEVICE_ID, &req.header, sizeof(req), &resp.header, sizeof(resp));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: PDEV_GET_DEVICE_INFO failed %d\n", __func__, status);
            return status;
        }
        const pdev_device_info_t& info = resp.device_info;

        zx_device_prop_t props[] = {
            {BIND_PLATFORM_DEV_VID, 0, info.vid},
            {BIND_PLATFORM_DEV_PID, 0, info.pid},
            {BIND_PLATFORM_DEV_DID, 0, info.did},
        };

        device_add_args_t args = {};
        args.version = DEVICE_ADD_ARGS_VERSION;
        args.name = info.name;
        args.proto_id = ZX_PROTOCOL_PDEV;
        args.props = props;
        args.prop_count = fbl::count_of(props);

        status = ProxyDevice::CreateChild(zxdev(), ROOT_DEVICE_ID, fbl::RefPtr<PlatformProxy>(this),
                                          &args, nullptr);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: ProxyDevice::Create failed %d\n", __func__, status);
            return status;
        }
    }

    return ZX_OK;
}

void PlatformProxy::UnregisterProtocol(uint32_t proto_id) {
    protocols_.erase(proto_id);
}

zx_status_t PlatformProxy::Proxy(
    const void* req_buffer, size_t req_size, const zx_handle_t* req_handle_list,
    size_t req_handle_count, void* out_resp_buffer, size_t resp_size, size_t* out_resp_actual,
    zx_handle_t* out_resp_handle_list, size_t resp_handle_count,
    size_t* out_resp_handle_actual) {

    auto* req = static_cast<const platform_proxy_req*>(req_buffer);
    auto* resp = static_cast<platform_proxy_rsp*>(out_resp_buffer);
    if (req->device_id != ROOT_DEVICE_ID) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (req_size > PLATFORM_PROXY_MAX_DATA) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return Rpc(ROOT_DEVICE_ID, req, req_size, resp, resp_size,
               req_handle_list, req_handle_count, out_resp_handle_list,
               resp_handle_count, out_resp_actual);
}

zx_status_t PlatformProxy::Create(zx_device_t* parent, zx_handle_t rpc_channel) {
    fbl::AllocChecker ac;

    auto proxy = fbl::MakeRefCountedChecked<PlatformProxy>(&ac, parent, rpc_channel);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return proxy->Init(parent);
}

zx_status_t PlatformProxy::Init(zx_device_t* parent) {
    // Get list of extra protocols to proxy.
    rpc_pdev_req_t req = {};
    struct {
        rpc_pdev_rsp_t pdev;
        uint32_t protocols[PROXY_MAX_PROTOCOLS];
    } resp = {};
    req.header.device_id = ROOT_DEVICE_ID;
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_PROTOCOLS;

    auto status = Rpc(ROOT_DEVICE_ID, &req.header, sizeof(req), &resp.pdev.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }

    if (resp.pdev.protocol_count > 0) {
        status = DdkAdd("PlatformProxy");
        if (status != ZX_OK) {
            return status;
        }
        // Increment our reference count so we aren't destroyed while the devmgr
        // has a reference to us. We will decrement it in DdkRelease();
        AddRef();

        // Create device hosts for all the protocols.
        protocol_count_ = resp.pdev.protocol_count;

        for (uint32_t i = 0; i < protocol_count_; i++) {
            status = ProxyClient::Create(resp.protocols[i], zxdev(),
                                         fbl::RefPtr<PlatformProxy>(this));
        }
    } else {
        status = ProxyDevice::CreateRoot(parent, fbl::RefPtr<PlatformProxy>(this));
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

} // namespace platform_bus

zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    return platform_bus::PlatformProxy::Create(parent, rpc_channel);
}
