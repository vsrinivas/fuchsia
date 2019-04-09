// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "utils.h"

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/tap/cpp/fidl.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>

namespace wlan {

// Functions for converting between FIDL classes and related wlan C structs
uint16_t ConvertSupportedPhys(const ::std::vector<::fuchsia::wlan::device::SupportedPhy>& phys);
uint32_t ConvertDriverFeatures(
    const ::std::vector<::fuchsia::wlan::common::DriverFeature>& dfs);
uint16_t ConvertMacRole(::fuchsia::wlan::device::MacRole role);
::fuchsia::wlan::device::MacRole ConvertMacRole(uint16_t role);
uint16_t ConvertMacRoles(::std::vector<::fuchsia::wlan::device::MacRole>& role);
uint32_t ConvertCaps(const ::std::vector<::fuchsia::wlan::device::Capability>& caps);
void ConvertBandInfo(const ::fuchsia::wlan::device::BandInfo& in, wlan_band_info_t* out);
zx_status_t ConvertPhyInfo(wlan_info_t* out, const ::fuchsia::wlan::device::PhyInfo& in);
wlan_tx_status_t ConvertTxStatus(const ::fuchsia::wlan::tap::WlanTxStatus& in);
}  // namespace wlan

