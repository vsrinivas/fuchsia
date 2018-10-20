// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/hidbus.banjo INSTEAD.

#pragma once

#include <ddk/protocol/hidbus.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_ifc_io_queue, HidbusIfcIoQueue,
                                     void (C::*)(const void* buf_buffer, size_t buf_size));

template <typename D>
constexpr void CheckHidbusIfcSubclass() {
    static_assert(internal::has_hidbus_ifc_io_queue<D>::value,
                  "HidbusIfc subclasses must implement "
                  "void HidbusIfcIoQueue(const void* buf_buffer, size_t buf_size");
}

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_query, HidbusQuery,
                                     zx_status_t (C::*)(uint32_t options, hid_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_start, HidbusStart,
                                     zx_status_t (C::*)(const hidbus_ifc_t* ifc));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_stop, HidbusStop, void (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_get_descriptor, HidbusGetDescriptor,
                                     zx_status_t (C::*)(hid_description_type_t desc_type,
                                                        void** out_data_buffer, size_t* data_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_get_report, HidbusGetReport,
                                     zx_status_t (C::*)(hid_report_type_t rpt_type, uint8_t rpt_id,
                                                        void* out_data_buffer, size_t data_size,
                                                        size_t* out_data_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_set_report, HidbusSetReport,
                                     zx_status_t (C::*)(hid_report_type_t rpt_type, uint8_t rpt_id,
                                                        const void* data_buffer, size_t data_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_get_idle, HidbusGetIdle,
                                     zx_status_t (C::*)(uint8_t rpt_id, uint8_t* out_duration));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_set_idle, HidbusSetIdle,
                                     zx_status_t (C::*)(uint8_t rpt_id, uint8_t duration));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_get_protocol, HidbusGetProtocol,
                                     zx_status_t (C::*)(hid_protocol_t* out_protocol));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hidbus_protocol_set_protocol, HidbusSetProtocol,
                                     zx_status_t (C::*)(hid_protocol_t protocol));

template <typename D>
constexpr void CheckHidbusProtocolSubclass() {
    static_assert(internal::has_hidbus_protocol_query<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info");
    static_assert(internal::has_hidbus_protocol_start<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusStart(const hidbus_ifc_t* ifc");
    static_assert(internal::has_hidbus_protocol_stop<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "void HidbusStop(");
    static_assert(internal::has_hidbus_protocol_get_descriptor<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void** "
                  "out_data_buffer, size_t* data_size");
    static_assert(internal::has_hidbus_protocol_get_report<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* "
                  "out_data_buffer, size_t data_size, size_t* out_data_actual");
    static_assert(internal::has_hidbus_protocol_set_report<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const "
                  "void* data_buffer, size_t data_size");
    static_assert(internal::has_hidbus_protocol_get_idle<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration");
    static_assert(internal::has_hidbus_protocol_set_idle<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration");
    static_assert(internal::has_hidbus_protocol_get_protocol<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol");
    static_assert(internal::has_hidbus_protocol_set_protocol<D>::value,
                  "HidbusProtocol subclasses must implement "
                  "zx_status_t HidbusSetProtocol(hid_protocol_t protocol");
}

} // namespace internal
} // namespace ddk
