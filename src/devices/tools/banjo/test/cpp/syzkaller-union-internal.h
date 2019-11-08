// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.union banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_packet, ApiPacket,
        zx_status_t (C::*)(zx::handle h, const packet_t* pkt));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_packet<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiPacket(zx::handle h, const packet_t* pkt);");

}


} // namespace internal
} // namespace ddk
