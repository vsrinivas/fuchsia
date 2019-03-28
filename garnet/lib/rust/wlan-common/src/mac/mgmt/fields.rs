// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mac::{
        FrameControl, HtControl, MacAddr, OptionalField, Presence, ReasonCode, SequenceControl,
        StatusCode,
    },
    wlan_bitfield::bitfield,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

// IEEE Std 802.11-2016, 9.4.1.4
#[bitfield(
    0       ess,
    1       ibss,
    2       cf_pollable,
    3       cf_poll_req,
    4       privacy,
    5       short_preamble,
    6..=7   _,  // reserved
    8       spectrum_mgmt,
    9       qos,
    10      short_slot_time,
    11      apsd,
    12      radio_measurement,
    13      _,  // reserved
    14      delayed_block_ack,
    15      immediate_block_ack,
)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy)]
#[repr(C)]
pub struct CapabilityInfo(pub u16);

// IEEE Std 802.11-2016, 9.3.3.2
#[derive(FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct MgmtHdr {
    pub frame_ctrl: FrameControl,
    pub duration: u16,
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
    pub seq_ctrl: SequenceControl,
}

impl MgmtHdr {
    /// Returns the length in bytes of a mgmt header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(has_ht_ctrl: Presence<HtControl>) -> usize {
        let mut bytes = std::mem::size_of::<MgmtHdr>();
        bytes += match has_ht_ctrl {
            HtControl::PRESENT => std::mem::size_of::<HtControl>(),
            HtControl::ABSENT => 0,
        };
        bytes
    }
}

// IEEE Std 802.11-2016, 9.4.1.1
#[repr(C)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Copy, Clone, Debug, Default)]
pub struct AuthAlgorithmNumber(pub u16);

impl AuthAlgorithmNumber {
    pub const OPEN: Self = Self(0);
    pub const SHARED_KEY: Self = Self(1);
    pub const FAST_BSS_TRANSITION: Self = Self(2);
    pub const SAE: Self = Self(3);
    // 4-65534 Reserved
    pub const VENDOR_SPECIFIC: Self = Self(65535);
}

// IEEE Std 802.11-2016, 9.3.3.3
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct BeaconHdr {
    pub timestamp: u64,
    pub beacon_interval: u16,
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: CapabilityInfo,
}

// IEEE Std 802.11-2016, 9.3.3.12
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AuthHdr {
    pub auth_alg_num: AuthAlgorithmNumber,
    pub auth_txn_seq_num: u16,
    pub status_code: StatusCode,
}

// IEEE Std 802.11-2016, 9.3.3.13
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct DeauthHdr {
    pub reason_code: ReasonCode,
}

// IEEE Std 802.11-2016, 9.3.3.6
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AssocRespHdr {
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: CapabilityInfo,
    pub status_code: StatusCode,
    pub aid: u16,
}
