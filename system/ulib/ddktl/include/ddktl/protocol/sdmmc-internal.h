// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/sdmmc.h>
#include <fbl/type_support.h>

#include <stdint.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_host_info, HostInfo,
                                     zx_status_t (C::*)(sdmmc_host_info_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_set_signal_voltage, SetSignalVoltage,
                                     zx_status_t (C::*)(sdmmc_voltage_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_set_bus_width, SetBusWidth,
                                     zx_status_t (C::*)(sdmmc_bus_width_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_set_bus_freq, SetBusFreq, zx_status_t (C::*)(uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_set_timing, SetTiming, zx_status_t (C::*)(sdmmc_timing_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hw_reset, HwReset, void (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_perform_tuning, PerformTuning,
                                     zx_status_t (C::*)(uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_request, Request, zx_status_t (C::*)(sdmmc_req_t*));

template <typename D>
constexpr void CheckSdMmcProtocolSubclass() {
    static_assert(internal::has_host_info<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "HostInfo(sdmmc_host_info_t* info)");
    static_assert(internal::has_set_signal_voltage<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "SetSignalVoltage(sdmmc_voltage_t voltage)");
    static_assert(internal::has_set_bus_width<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "SetBusWidth(sdmmc_bus_width_t bus_width)");
    static_assert(internal::has_set_bus_freq<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "SetBusFreq(uint32_t bus_freq)");
    static_assert(internal::has_set_timing<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "SetTiming(sdmmc_timing_t timing)");
    static_assert(internal::has_hw_reset<D>::value,
                  "SdMmcProtocol subclasses must implement HwReset()");
    static_assert(internal::has_perform_tuning<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t "
                  "PerformTuning(uint32_t cmd_idx)");
    static_assert(internal::has_request<D>::value,
                  "SdMmcProtocol subclasses must implement zx_status_t Request(sdmmc_req_t* req)");
}

}  // namespace internal
}  // namespace ddk
