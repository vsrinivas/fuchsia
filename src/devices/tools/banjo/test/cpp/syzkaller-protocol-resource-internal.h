// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.resources banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::handle* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::process* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::process h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::thread* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::thread h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::vmo h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::channel* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::channel h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::event* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::event h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::port* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::port h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::interrupt* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::interrupt h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::log* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::log h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::socket* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::socket h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::resource* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::resource h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::eventpair* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::eventpair h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::job* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::job h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::vmar* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::vmar h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::fifo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::fifo h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::guest* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::guest h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::guest* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::guest h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::timer* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::timer h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::bti* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::bti h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::profile* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::profile h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::debuglog* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::debuglog h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::vcpu* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::vcpu h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::iommu* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::iommu h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::pager* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::pager h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_status_t (C::*)(uint32_t options, zx::pmt* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx::pmt h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_time_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_time_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_time_t t));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_duration_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_duration_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_duration_t d));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_clock_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_clock_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_clock_t cid));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_koid_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_koid_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_koid_t id));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_vaddr_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_vaddr_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_vaddr_t va));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_paddr_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_paddr_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_paddr_t pa));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_paddr32_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_paddr32_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_paddr32_t pa32));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_gpaddr_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_gpaddr_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_gpaddr_t gpa));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_off_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_off_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_off_t o));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_rights_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_rights_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_rights_t r));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_signals_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_signals_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_signals_t s));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer, ApiProducer,
        zx_vm_option_t (C::*)(zx::handle h));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_producer2, ApiProducer2,
        zx_status_t (C::*)(zx::handle h, zx_vm_option_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_consumer, ApiConsumer,
        zx_status_t (C::*)(zx_vm_option_t op));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::handle* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::process* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::process h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::thread* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::thread h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::vmo h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::channel* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::channel h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::event* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::event h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::port* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::port h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::interrupt* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::interrupt h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::log* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::log h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::socket* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::socket h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::resource* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::resource h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::eventpair* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::eventpair h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::job* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::job h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::vmar* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::vmar h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::fifo* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::fifo h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::guest* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::guest h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::guest* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::guest h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::timer* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::timer h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::bti* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::bti h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::profile* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::profile h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::debuglog* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::debuglog h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::vcpu* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::vcpu h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::iommu* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::iommu h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::pager* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::pager h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer(uint32_t options, zx::pmt* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx::pmt h);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_time_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_time_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_time_t t);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_duration_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_duration_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_duration_t d);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_clock_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_clock_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_clock_t cid);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_koid_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_koid_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_koid_t id);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_vaddr_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_vaddr_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_vaddr_t va);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_paddr_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_paddr_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_paddr_t pa);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_paddr32_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_paddr32_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_paddr32_t pa32);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_gpaddr_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_gpaddr_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_gpaddr_t gpa);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_off_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_off_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_off_t o);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_rights_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_rights_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_rights_t r);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_signals_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_signals_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_signals_t s);");

    static_assert(internal::has_api_protocol_producer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_vm_option_t ApiProducer(zx::handle h);");

    static_assert(internal::has_api_protocol_producer2<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiProducer2(zx::handle h, zx_vm_option_t* out_out);");

    static_assert(internal::has_api_protocol_consumer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiConsumer(zx_vm_option_t op);");

}


} // namespace internal
} // namespace ddk
