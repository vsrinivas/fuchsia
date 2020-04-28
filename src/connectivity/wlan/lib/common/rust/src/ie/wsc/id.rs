// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes, Unaligned};

#[repr(C, packed)]
#[derive(Eq, PartialEq, Hash, AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub struct Id(pub [u8; 2]);

/// WSC Technical Specification v2.0.7, Section 12, Table 28
impl Id {
    pub const AP_SETUP_LOCKED: Self = Self([0x10, 0x57]);
    pub const CONFIG_METHODS: Self = Self([0x10, 0x08]);
    pub const DEVICE_NAME: Self = Self([0x10, 0x11]);
    pub const DEVICE_PASSWORD_ID: Self = Self([0x10, 0x12]);
    pub const MANUFACTURER: Self = Self([0x10, 0x21]);
    pub const MODEL_NAME: Self = Self([0x10, 0x23]);
    pub const MODEL_NUMBER: Self = Self([0x10, 0x24]);
    pub const PRIMARY_DEVICE_TYPE: Self = Self([0x10, 0x54]);
    pub const RESPONSE_TYPE: Self = Self([0x10, 0x3B]);
    pub const RF_BANDS: Self = Self([0x10, 0x3C]);
    pub const SELECTED_REG: Self = Self([0x10, 0x41]);
    pub const SELECTED_REG_CONFIG_METHODS: Self = Self([0x10, 0x53]);
    pub const SERIAL_NUMBER: Self = Self([0x10, 0x42]);
    pub const UUID_E: Self = Self([0x10, 0x47]);
    pub const VENDOR_EXT: Self = Self([0x10, 0x49]);
    pub const VERSION: Self = Self([0x10, 0x4A]);
    pub const WPS_STATE: Self = Self([0x10, 0x44]);
}
