// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.zx banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_status, ApiStatus,
        zx_status_t (C::*)(zx_status_t st));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_time, ApiTime,
        zx_time_t (C::*)(zx_time_t t));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_duration, ApiDuration,
        zx_duration_t (C::*)(zx_duration_t d));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_clock, ApiClock,
        zx_clock_t (C::*)(zx_clock_t cid));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_koid, ApiKoid,
        zx_koid_t (C::*)(zx_koid_t id));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vaddr, ApiVaddr,
        zx_vaddr_t (C::*)(zx_vaddr_t va));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_paddr, ApiPaddr,
        zx_paddr_t (C::*)(zx_paddr_t pa));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_paddr32, ApiPaddr32,
        zx_paddr32_t (C::*)(zx_paddr32_t pa32));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_gpaddr, ApiGpaddr,
        zx_gpaddr_t (C::*)(zx_gpaddr_t gpa));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_off, ApiOff,
        zx_off_t (C::*)(zx_off_t o));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_rights, ApiRights,
        zx_rights_t (C::*)(zx_rights_t r));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_signals, ApiSignals,
        zx_signals_t (C::*)(zx_signals_t sig));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vm_option, ApiVmOption,
        zx_vm_option_t (C::*)(zx_vm_option_t op));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_status<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiStatus(zx_status_t st);");

    static_assert(internal::has_api_protocol_time<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_time_t ApiTime(zx_time_t t);");

    static_assert(internal::has_api_protocol_duration<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_duration_t ApiDuration(zx_duration_t d);");

    static_assert(internal::has_api_protocol_clock<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_clock_t ApiClock(zx_clock_t cid);");

    static_assert(internal::has_api_protocol_koid<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_koid_t ApiKoid(zx_koid_t id);");

    static_assert(internal::has_api_protocol_vaddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_vaddr_t ApiVaddr(zx_vaddr_t va);");

    static_assert(internal::has_api_protocol_paddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_paddr_t ApiPaddr(zx_paddr_t pa);");

    static_assert(internal::has_api_protocol_paddr32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_paddr32_t ApiPaddr32(zx_paddr32_t pa32);");

    static_assert(internal::has_api_protocol_gpaddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_gpaddr_t ApiGpaddr(zx_gpaddr_t gpa);");

    static_assert(internal::has_api_protocol_off<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_off_t ApiOff(zx_off_t o);");

    static_assert(internal::has_api_protocol_rights<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_rights_t ApiRights(zx_rights_t r);");

    static_assert(internal::has_api_protocol_signals<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_signals_t ApiSignals(zx_signals_t sig);");

    static_assert(internal::has_api_protocol_vm_option<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_vm_option_t ApiVmOption(zx_vm_option_t op);");

}


} // namespace internal
} // namespace ddk
