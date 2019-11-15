// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(29063): This should be eventually replaced with Banjo structs. For now, they just match the
// C layout of Banjo structs.

// LINT.IfChange
#[repr(C, packed)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub struct Ieee80211HtCapabilitiesSupportedMcsSetFields {
    pub rx_mcs_head: u64,
    pub rx_mcs_tail: u32,
    pub tx_mcs: u32,
}

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub union Ieee80211HtCapabilitiesSupportedMcsSet {
    pub bytes: [u8; 16],
    pub fields: Ieee80211HtCapabilitiesSupportedMcsSetFields,
}

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct Ieee80211HtCapabilities {
    pub ht_capability_info: u16,
    pub ampdu_params: u8,
    pub supported_mcs_set: Ieee80211HtCapabilitiesSupportedMcsSet,
    pub ext_capabilities: u16,
    pub tx_beamforming_capabilities: u32,
    pub asel_capabilities: u8,
}

#[repr(C, packed)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub struct Ieee80211VhtCapabilities {
    pub vht_capability_info: u32,
    pub supported_vht_mcs_and_nss_set: u64,
}
// LINT.ThenChange(//zircon/system/banjo/ddk.hw.wlan.ieee80211/ieee80211.banjo)
