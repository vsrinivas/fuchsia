// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.clock banjo file

#ifndef SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_INTERNAL_H_
#define SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_INTERNAL_H_

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_enable, ClockEnable,
                                                    zx_status_t (C::*)());

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_disable, ClockDisable,
                                                    zx_status_t (C::*)());

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_is_enabled, ClockIsEnabled,
                                                    zx_status_t (C::*)(bool* out_enabled));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_set_rate, ClockSetRate,
                                                    zx_status_t (C::*)(uint64_t hz));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_query_supported_rate,
                                                    ClockQuerySupportedRate,
                                                    zx_status_t (C::*)(uint64_t hz_in,
                                                                       uint64_t* out_hz_out));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_get_rate, ClockGetRate,
                                                    zx_status_t (C::*)(uint64_t* out_hz));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_set_input, ClockSetInput,
                                                    zx_status_t (C::*)(uint32_t idx));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_get_num_inputs,
                                                    ClockGetNumInputs,
                                                    zx_status_t (C::*)(uint32_t* out_n));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clock_protocol_get_input, ClockGetInput,
                                                    zx_status_t (C::*)(uint32_t* out_index));

template <typename D>
constexpr void CheckClockProtocolSubclass() {
  static_assert(internal::has_clock_protocol_enable<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockEnable();");

  static_assert(internal::has_clock_protocol_disable<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockDisable();");

  static_assert(internal::has_clock_protocol_is_enabled<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockIsEnabled(bool* out_enabled);");

  static_assert(internal::has_clock_protocol_set_rate<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockSetRate(uint64_t hz);");

  static_assert(internal::has_clock_protocol_query_supported_rate<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockQuerySupportedRate(uint64_t hz_in, uint64_t* out_hz_out);");

  static_assert(internal::has_clock_protocol_get_rate<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockGetRate(uint64_t* out_hz);");

  static_assert(internal::has_clock_protocol_set_input<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockSetInput(uint32_t idx);");

  static_assert(internal::has_clock_protocol_get_num_inputs<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockGetNumInputs(uint32_t* out_n);");

  static_assert(internal::has_clock_protocol_get_input<D>::value,
                "ClockProtocol subclasses must implement "
                "zx_status_t ClockGetInput(uint32_t* out_index);");
}

}  // namespace internal
}  // namespace ddk

#endif  // SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_INTERNAL_H_
