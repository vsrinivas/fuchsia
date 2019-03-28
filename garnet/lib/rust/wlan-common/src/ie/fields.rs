// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    wlan_bitfield::bitfield,
    zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned},
};

// IEEE Std 802.11-2016, 9.4.2.3
#[bitfield(
    0..=6   rate,
    7       basic,
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
pub struct SupportedRate(pub u8);

// IEEE Std 802.11-2016, 9.4.2.4
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct DsssParamSet {
    pub current_chan: u8,
}

// IEEE Std 802.11-2016, 9.2.4.6
#[bitfield(
    0       group_traffic,
    1..=7   offset,
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
pub struct BitmapControl(pub u8);

// IEEE Std 802.11-2016, 9.4.2.6
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct TimHeader {
    pub dtim_count: u8,
    pub dtim_period: u8,
    pub bmp_ctrl: BitmapControl,
}

pub struct TimView<B> {
    pub header: LayoutVerified<B, TimHeader>,
    pub bitmap: B,
}
