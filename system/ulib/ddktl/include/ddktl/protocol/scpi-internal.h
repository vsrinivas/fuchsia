// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_get_sensor, ScpiGetSensor,
        zx_status_t (C::*)(const char*, uint32_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_get_sensor_value, ScpiGetSensorValue,
        zx_status_t (C::*)(uint32_t, uint32_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_get_dvfs_info, ScpiGetDvfsInfo,
        zx_status_t (C::*)(uint8_t, scpi_opp_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_get_dvfs_idx, ScpiGetDvfsIdx,
        zx_status_t (C::*)(uint8_t, uint16_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_scpi_set_dvfs_idx, ScpiSetDvfsIdx,
        zx_status_t (C::*)(uint8_t, uint16_t));

template <typename D>
constexpr void CheckScpiProtocolSubclass() {
    static_assert(internal::has_scpi_get_sensor<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "ScpiGetSensor(const char* name, uint32_t* sensor_id)");
    static_assert(internal::has_scpi_get_sensor_value<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value)");
    static_assert(internal::has_scpi_get_dvfs_info<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps)");
    static_assert(internal::has_scpi_get_dvfs_idx<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx)");
    static_assert(internal::has_scpi_set_dvfs_idx<D>::value,
                  "ScpiProtocol subclasses must implement "
                  "ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx)");
 }

}  // namespace internal
}  // namespace ddk
