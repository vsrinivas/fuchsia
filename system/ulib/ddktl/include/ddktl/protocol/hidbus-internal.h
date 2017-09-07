// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <magenta/types.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

#include <stdint.h>

namespace ddk {

class HidBusIfcProxy;

namespace internal {

DECLARE_HAS_MEMBER_FN(has_hidbus_query, HidBusQuery);
DECLARE_HAS_MEMBER_FN(has_hidbus_start, HidBusStart);
DECLARE_HAS_MEMBER_FN(has_hidbus_stop, HidBusStop);
DECLARE_HAS_MEMBER_FN(has_hidbus_get_descriptor, HidBusGetDescriptor);
DECLARE_HAS_MEMBER_FN(has_hidbus_get_report, HidBusGetReport);
DECLARE_HAS_MEMBER_FN(has_hidbus_set_report, HidBusSetReport);
DECLARE_HAS_MEMBER_FN(has_hidbus_get_idle, HidBusGetIdle);
DECLARE_HAS_MEMBER_FN(has_hidbus_set_idle, HidBusSetIdle);
DECLARE_HAS_MEMBER_FN(has_hidbus_get_protocol, HidBusGetProtocol);
DECLARE_HAS_MEMBER_FN(has_hidbus_set_protocol, HidBusSetProtocol);

template <typename D>
constexpr void CheckHidBusProtocolSubclass() {
    static_assert(internal::has_hidbus_query<D>::value,
                  "HidBusProtocol subclasses must implement HidBusQuery");
    static_assert(fbl::is_same<decltype(&D::HidBusQuery),
                                mx_status_t (D::*)(uint32_t options, hid_info_t* info)>::value,
                  "HidBusQuery must be a non-static member function with signature "
                  "'mx_status_t HidBusQuery(uint32_t options, hid_info_t* info)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_start<D>::value,
                  "HidBusProtocol subclasses must implement HidBusStart");
    static_assert(fbl::is_same<decltype(&D::HidBusStart),
                                mx_status_t (D::*)(HidBusIfcProxy proxy)>::value,
                  "HidBusStart must be a non-static member function with signature "
                  "'mx_status_t HidBusStart(HidBusIfcProxy proxy)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_stop<D>::value,
                  "HidBusProtocol subclasses must implement HidBusStop");
    static_assert(fbl::is_same<decltype(&D::HidBusStop),
                                void (D::*)()>::value,
                  "HidBusStop must be a non-static member function with signature "
                  "'void HidBusStop()', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_get_descriptor<D>::value,
                  "HidBusProtocol subclasses must implement HidBusGetDescriptor");
    static_assert(fbl::is_same<decltype(&D::HidBusGetDescriptor),
                                mx_status_t (D::*)(uint8_t desc_type, void** data, size_t* len)>::value,
                  "HidBusGetDescriptor must be a non-static member function with signature "
                  "'mx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_get_report<D>::value,
                  "HidBusProtocol subclasses must implement HidBusGetReport");
    static_assert(fbl::is_same<decltype(&D::HidBusGetReport),
                                mx_status_t (D::*)(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len)>::value,
                  "HidBusGetReport must be a non-static member function with signature "
                  "'mx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_set_report<D>::value,
                  "HidBusProtocol subclasses must implement HidBusSetReport");
    static_assert(fbl::is_same<decltype(&D::HidBusSetReport),
                                mx_status_t (D::*)(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len)>::value,
                  "HidBusSetReport must be a non-static member function with signature "
                  "'mx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_get_idle<D>::value,
                  "HidBusProtocol subclasses must implement HidBusGetIdle");
    static_assert(fbl::is_same<decltype(&D::HidBusGetIdle),
                                mx_status_t (D::*)(uint8_t rpt_id, uint8_t* duration)>::value,
                  "HidBusGetIdle must be a non-static member function with signature "
                  "'mx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_set_idle<D>::value,
                  "HidBusProtocol subclasses must implement HidBusSetIdle");
    static_assert(fbl::is_same<decltype(&D::HidBusSetIdle),
                                mx_status_t (D::*)(uint8_t rpt_id, uint8_t duration)>::value,
                  "HidBusSetIdle must be a non-static member function with signature "
                  "'mx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_get_protocol<D>::value,
                  "HidBusProtocol subclasses must implement HidBusGetProtocol");
    static_assert(fbl::is_same<decltype(&D::HidBusGetProtocol),
                                mx_status_t (D::*)(uint8_t* protocol)>::value,
                  "HidBusGetProtocol must be a non-static member function with signature "
                  "'mx_status_t HidBusGetProtocol(uint8_t* protocol)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");

    static_assert(internal::has_hidbus_set_protocol<D>::value,
                  "HidBusProtocol subclasses must implement HidBusSetProtocol");
    static_assert(fbl::is_same<decltype(&D::HidBusSetProtocol),
                                mx_status_t (D::*)(uint8_t protocol)>::value,
                  "HidBusSetProtocol must be a non-static member function with signature "
                  "'mx_status_t HidBusSetProtocol(uint8_t protocol)', and be visible to "
                  "ddk::HidBusProtocol<D> (either because they are public, or because of "
                  "friendship).");
}

}  // namespace internal
}  // namespace ddk
