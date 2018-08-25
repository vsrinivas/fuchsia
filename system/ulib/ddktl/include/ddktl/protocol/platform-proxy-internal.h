// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_platform_proxy_register_protocol, RegisterProtocol,
        zx_status_t (C::*)(uint32_t proto_id, const void* protocol));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_platform_proxy_proxy, Proxy,
        zx_status_t (C::*)(platform_proxy_args_t* args));

template <typename D>
constexpr void CheckPlatformProxyProtocolSubclass() {
    static_assert(internal::has_platform_proxy_register_protocol<D>::value,
                  "PlatformProxyProtocol subclasses must implement "
                  "RegisterProtocol(uint32_t proto_id, const void* protocol)");
    static_assert(internal::has_platform_proxy_proxy<D>::value,
                  "PlatformProxyProtocol subclasses must implement "
                  "Proxy(platform_proxy_args_t* args)");
 }

}  // namespace internal
}  // namespace ddk
