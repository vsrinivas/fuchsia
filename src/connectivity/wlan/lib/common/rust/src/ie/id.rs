// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes, Unaligned};

#[repr(C, packed)]
#[derive(Eq, PartialEq, Hash, AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub struct Id(pub u8);

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
impl Id {
    pub const SSID: Self = Self(0);
    pub const SUPPORTED_RATES: Self = Self(1);
    pub const DSSS_PARAM_SET: Self = Self(3);
    pub const TIM: Self = Self(5);
    pub const COUNTRY: Self = Self(7);
    pub const HT_CAPABILITIES: Self = Self(45);
    pub const RSNE: Self = Self(48);
    pub const EXT_SUPPORTED_RATES: Self = Self(50);
    pub const HT_OPERATION: Self = Self(61);
    pub const MESH_PEERING_MGMT: Self = Self(117);
    pub const PREQ: Self = Self(130);
    pub const PREP: Self = Self(131);
    pub const PERR: Self = Self(132);
    pub const VHT_CAPABILITIES: Self = Self(191);
    pub const VHT_OPERATION: Self = Self(192);
    pub const VENDOR_SPECIFIC: Self = Self(221);
}
