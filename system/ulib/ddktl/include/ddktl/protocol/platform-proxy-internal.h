// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_proxy.banjo INSTEAD.

#pragma once

#include <ddk/protocol/platform-proxy.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_platform_proxy_protocol_register_protocol, PlatformProxyRegisterProtocol,
    zx_status_t (C::*)(uint32_t proto_id, const void* protocol_buffer, size_t protocol_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_platform_proxy_protocol_proxy, PlatformProxyProxy,
    zx_status_t (C::*)(const void* req_buffer, size_t req_size, const zx_handle_t* req_handle_list,
                       size_t req_handle_count, void* out_resp_buffer, size_t resp_size,
                       size_t* out_resp_actual, zx_handle_t* out_resp_handle_list,
                       size_t resp_handle_count, size_t* out_resp_handle_actual));

template <typename D>
constexpr void CheckPlatformProxyProtocolSubclass() {
    static_assert(internal::has_platform_proxy_protocol_register_protocol<D>::value,
                  "PlatformProxyProtocol subclasses must implement "
                  "zx_status_t PlatformProxyRegisterProtocol(uint32_t proto_id, const void* "
                  "protocol_buffer, size_t protocol_size");
    static_assert(internal::has_platform_proxy_protocol_proxy<D>::value,
                  "PlatformProxyProtocol subclasses must implement "
                  "zx_status_t PlatformProxyProxy(const void* req_buffer, size_t req_size, const "
                  "zx_handle_t* req_handle_list, size_t req_handle_count, void* out_resp_buffer, "
                  "size_t resp_size, size_t* out_resp_actual, zx_handle_t* out_resp_handle_list, "
                  "size_t resp_handle_count, size_t* out_resp_handle_actual");
}

} // namespace internal
} // namespace ddk
