// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolbase banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_status, SynchronousBaseStatus,
        zx_status_t (C::*)(zx_status_t status, zx_status_t* out_status_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_time, SynchronousBaseTime,
        zx_time_t (C::*)(zx_time_t time, zx_time_t* out_time_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_duration, SynchronousBaseDuration,
        zx_duration_t (C::*)(zx_duration_t duration, zx_duration_t* out_duration_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_koid, SynchronousBaseKoid,
        zx_koid_t (C::*)(zx_koid_t koid, zx_koid_t* out_koid_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_vaddr, SynchronousBaseVaddr,
        zx_vaddr_t (C::*)(zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_paddr, SynchronousBasePaddr,
        zx_paddr_t (C::*)(zx_paddr_t paddr, zx_paddr_t* out_paddr_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_paddr32, SynchronousBasePaddr32,
        zx_paddr32_t (C::*)(zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_gpaddr, SynchronousBaseGpaddr,
        zx_gpaddr_t (C::*)(zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_base_protocol_off, SynchronousBaseOff,
        zx_off_t (C::*)(zx_off_t off, zx_off_t* out_off_2));


template <typename D>
constexpr void CheckSynchronousBaseProtocolSubclass() {
    static_assert(internal::has_synchronous_base_protocol_status<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_status_t SynchronousBaseStatus(zx_status_t status, zx_status_t* out_status_2);");

    static_assert(internal::has_synchronous_base_protocol_time<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_time_t SynchronousBaseTime(zx_time_t time, zx_time_t* out_time_2);");

    static_assert(internal::has_synchronous_base_protocol_duration<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_duration_t SynchronousBaseDuration(zx_duration_t duration, zx_duration_t* out_duration_2);");

    static_assert(internal::has_synchronous_base_protocol_koid<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_koid_t SynchronousBaseKoid(zx_koid_t koid, zx_koid_t* out_koid_2);");

    static_assert(internal::has_synchronous_base_protocol_vaddr<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_vaddr_t SynchronousBaseVaddr(zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2);");

    static_assert(internal::has_synchronous_base_protocol_paddr<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_paddr_t SynchronousBasePaddr(zx_paddr_t paddr, zx_paddr_t* out_paddr_2);");

    static_assert(internal::has_synchronous_base_protocol_paddr32<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_paddr32_t SynchronousBasePaddr32(zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2);");

    static_assert(internal::has_synchronous_base_protocol_gpaddr<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_gpaddr_t SynchronousBaseGpaddr(zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2);");

    static_assert(internal::has_synchronous_base_protocol_off<D>::value,
        "SynchronousBaseProtocol subclasses must implement "
        "zx_off_t SynchronousBaseOff(zx_off_t off, zx_off_t* out_off_2);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_status, AsyncBaseStatus,
        void (C::*)(zx_status_t status, async_base_status_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_time, AsyncBaseTime,
        void (C::*)(zx_time_t time, async_base_time_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_duration, AsyncBaseDuration,
        void (C::*)(zx_duration_t duration, async_base_duration_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_koid, AsyncBaseKoid,
        void (C::*)(zx_koid_t koid, async_base_koid_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_vaddr, AsyncBaseVaddr,
        void (C::*)(zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_paddr, AsyncBasePaddr,
        void (C::*)(zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_paddr32, AsyncBasePaddr32,
        void (C::*)(zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_gpaddr, AsyncBaseGpaddr,
        void (C::*)(zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_base_protocol_off, AsyncBaseOff,
        void (C::*)(zx_off_t off, async_base_off_callback callback, void* cookie));


template <typename D>
constexpr void CheckAsyncBaseProtocolSubclass() {
    static_assert(internal::has_async_base_protocol_status<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseStatus(zx_status_t status, async_base_status_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_time<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseTime(zx_time_t time, async_base_time_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_duration<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseDuration(zx_duration_t duration, async_base_duration_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_koid<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseKoid(zx_koid_t koid, async_base_koid_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_vaddr<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseVaddr(zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_paddr<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBasePaddr(zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_paddr32<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBasePaddr32(zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_gpaddr<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseGpaddr(zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie);");

    static_assert(internal::has_async_base_protocol_off<D>::value,
        "AsyncBaseProtocol subclasses must implement "
        "void AsyncBaseOff(zx_off_t off, async_base_off_callback callback, void* cookie);");

}


} // namespace internal
} // namespace ddk
