// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes, Unaligned};

/// WSC Technical Specification v2.0.7, Section 8.2.5, Table 7 and Section 12, Table 28
#[derive(Eq, PartialEq, Hash, Clone, Debug)]
pub struct ProbeRespWsc {
    pub version: u8,
    pub wps_state: WpsState,
    pub ap_setup_locked: bool,
    pub selected_reg: bool,
    pub selected_reg_config_methods: Option<[u8; 2]>,
    pub response_type: u8,
    pub uuid_e: [u8; 16],
    pub manufacturer: Vec<u8>,
    pub model_name: Vec<u8>,
    pub model_number: Vec<u8>,
    pub serial_number: Vec<u8>,
    pub primary_device_type: [u8; 8],
    pub device_name: Vec<u8>,
    pub config_methods: [u8; 2],
    pub rf_bands: Option<u8>,
    pub vendor_ext: Vec<u8>,
    // there may be other non-specific permitted attributes at the end, but we don't process them
}

#[repr(C, packed)]
#[derive(Eq, PartialEq, Hash, AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub struct WpsState(pub u8);

impl WpsState {
    pub const NOT_CONFIGURED: Self = Self(0x01);
    pub const CONFIGURED: Self = Self(0x02);
}
