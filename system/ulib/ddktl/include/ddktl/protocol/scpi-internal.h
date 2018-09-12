// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/scpi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/scpi.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_protocol_get_sensor, ScpiGetSensor,
                                     zx_status_t (C::*)(const char* name, uint32_t* out_sensor_id));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_protocol_get_sensor_value, ScpiGetSensorValue,
                                     zx_status_t (C::*)(uint32_t sensor_id,
                                                        uint32_t* out_sensor_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_protocol_get_dvfs_info, ScpiGetDvfsInfo,
                                     zx_status_t (C::*)(uint8_t power_domain,
                                                        scpi_opp_t* out_opps));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_protocol_get_dvfs_idx, ScpiGetDvfsIdx,
                                     zx_status_t (C::*)(uint8_t power_domain, uint16_t* out_index));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_protocol_set_dvfs_idx, ScpiSetDvfsIdx,
                                     zx_status_t (C::*)(uint8_t power_domain, uint16_t index));

template <typename D>
constexpr void CheckScpiProtocolSubclass() {
    static_assert(internal::has_scpi_protocol_get_sensor<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "zx_status_t ScpiGetSensor(const char* name, uint32_t* out_sensor_id");
    static_assert(internal::has_scpi_protocol_get_sensor_value<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* out_sensor_value");
    static_assert(internal::has_scpi_protocol_get_dvfs_info<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* out_opps");
    static_assert(internal::has_scpi_protocol_get_dvfs_idx<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* out_index");
    static_assert(internal::has_scpi_protocol_set_dvfs_idx<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t index");
}

} // namespace internal
} // namespace ddk
