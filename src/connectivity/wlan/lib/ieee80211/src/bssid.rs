// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

// Strictly speaking, the MAC address is not defined in 802.11, but it's defined
// here for convenience.
pub type MacAddr = [u8; fidl_ieee80211::MAC_ADDR_LEN as usize];

pub const NULL_MAC_ADDR: MacAddr = [0x00; fidl_ieee80211::MAC_ADDR_LEN as usize];

// Bssid is a newtype to wrap MacAddr where a BSSID is explicitly required, e.g. for beacon fields
// or management frame helper functions (e.g. ap::write_open_auth_frame and
// client::write_open_auth_frame).
#[repr(transparent)]
#[derive(
    FromBytes, AsBytes, Unaligned, Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash,
)]
pub struct Bssid(pub MacAddr);

pub const WILDCARD_BSSID: Bssid = Bssid([0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);
