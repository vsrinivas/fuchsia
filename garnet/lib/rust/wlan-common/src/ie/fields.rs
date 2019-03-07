// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

// IEEE Std 802.11-2016, 9.4.2.3
bitfield! {
    #[repr(C)]
    #[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy, Debug)]
    pub struct SupportedRate(u8);

    pub rate, set_rate: 6, 0;
    pub basic, set_basic: 7;

    pub value, _: 7,0;
}

// IEEE Std 802.11-2016, 9.4.2.4
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct DsssParamSet {
    pub current_chan: u8,
}

// IEEE Std 802.11-2016, 9.2.4.6
bitfield! {
    #[repr(C)]
    #[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy, Debug)]
    pub struct BitmapControl(u8);

    pub group_traffic, set_group_traffic: 0;
    pub offset, set_offset: 7, 1;

    pub value, _: 7,0;
}

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
