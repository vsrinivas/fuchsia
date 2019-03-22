// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mac::{FrameControl, HtControl, MacAddr, OptionalField, Presence, SequenceControl},
    byteorder::{ByteOrder, LittleEndian},
    wlan_bitfields::bitfields,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

pub type RawCapabilityInfo = [u8; 2];

// IEEE Std 802.11-2016, 9.4.1.4
#[bitfields(
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
#[repr(u16)]
#[derive(Copy, Clone)]
pub enum AuthAlgorithm {
    Open = 0,
    _SharedKey = 1,
    _FastBssTransition = 2,
    Sae = 3,
    // 4-65534 Reserved
    _VendorSpecific = 65535,
}

// IEEE Std 802.11-2016, 9.3.3.3
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct BeaconHdr {
    pub timestamp: [u8; 8],
    pub beacon_interval: [u8; 2],
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: RawCapabilityInfo,
}

impl BeaconHdr {
    pub fn timestamp(&self) -> u64 {
        LittleEndian::read_u64(&self.timestamp)
    }

    pub fn beacon_interval(&self) -> u16 {
        LittleEndian::read_u16(&self.beacon_interval)
    }

    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }
}

// IEEE Std 802.11-2016, 9.3.3.12
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AuthHdr {
    pub auth_alg_num: [u8; 2],
    pub auth_txn_seq_num: [u8; 2],
    pub status_code: [u8; 2],
}

impl AuthHdr {
    pub fn auth_alg_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_alg_num)
    }

    pub fn set_auth_alg_num(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.auth_alg_num, val)
    }

    pub fn auth_txn_seq_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_txn_seq_num)
    }

    pub fn set_auth_txn_seq_num(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.auth_txn_seq_num, val)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }

    pub fn set_status_code(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.status_code, val)
    }
}

// IEEE Std 802.11-2016, 9.3.3.13
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct DeauthHdr {
    pub reason_code: [u8; 2],
}

impl DeauthHdr {
    pub fn reason_code(&self) -> u16 {
        LittleEndian::read_u16(&self.reason_code)
    }

    pub fn set_reason_code(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.reason_code, val)
    }
}

// IEEE Std 802.11-2016, 9.3.3.6
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AssocRespHdr {
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: [u8; 2],
    pub status_code: [u8; 2],
    pub aid: [u8; 2],
}

impl AssocRespHdr {
    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }

    pub fn aid(&self) -> u16 {
        LittleEndian::read_u16(&self.aid)
    }
}
