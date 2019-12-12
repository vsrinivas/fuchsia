// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use wlan_common::{
    ie::{HtCapabilities, SupportedRate, VhtCapabilities},
    mac::CapabilityInfo,
};

/// Capabilities that takes the iface device's capabilities based on the channel a client is trying
/// to join, the PHY parameters that is overridden by user's command line input and the BSS the
/// client are is trying to join.
/// They are stored in the form of IEs because at some point they will be transmitted in
/// (Re)Association Request and (Re)Association Response frames.
#[derive(Debug)]
pub struct JoinCapabilities {
    pub cap_info: CapabilityInfo,
    pub rates: Vec<SupportedRate>,
    pub ht_cap: Option<HtCapabilities>,
    pub vht_cap: Option<VhtCapabilities>,
}
