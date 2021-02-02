// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.pass.callback banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_action_protocol_protocol_register_callback, ActionProtocolRegisterCallback,
        zx_status_t (C::*)(uint32_t id, const action_notify_t* cb));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_action_protocol_protocol_get_callback, ActionProtocolGetCallback,
        zx_status_t (C::*)(uint32_t id, action_notify_t* out_cb));


template <typename D>
constexpr void CheckActionProtocolProtocolSubclass() {
    static_assert(internal::has_action_protocol_protocol_register_callback<D>::value,
        "ActionProtocolProtocol subclasses must implement "
        "zx_status_t ActionProtocolRegisterCallback(uint32_t id, const action_notify_t* cb);");

    static_assert(internal::has_action_protocol_protocol_get_callback<D>::value,
        "ActionProtocolProtocol subclasses must implement "
        "zx_status_t ActionProtocolGetCallback(uint32_t id, action_notify_t* out_cb);");

}


} // namespace internal
} // namespace ddk
