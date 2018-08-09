// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "utils.h"

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <wlan/protocol/info.h>

namespace wlan_device = ::fuchsia::wlan::device;

// Functions for converting between FIDL classes and related wlan C structs
uint16_t ConvertSupportedPhys(const ::fidl::VectorPtr<wlan_device::SupportedPhy>& phys);
uint32_t ConvertDriverFeatures(const ::fidl::VectorPtr<wlan_device::DriverFeature>& dfs);
uint16_t ConvertMacRole(wlan_device::MacRole role);
wlan_device::MacRole ConvertMacRole(uint16_t role);
uint16_t ConvertMacRoles(::fidl::VectorPtr<wlan_device::MacRole>& role);
uint32_t ConvertCaps(const ::fidl::VectorPtr<wlan_device::Capability>& caps);
void ConvertBandInfo(const wlan_device::BandInfo& in, wlan_band_info_t* out);
zx_status_t ConvertPhyInfo(wlan_info_t* out, const wlan_device::PhyInfo& in);

