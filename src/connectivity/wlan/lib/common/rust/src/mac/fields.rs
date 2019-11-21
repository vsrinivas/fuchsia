// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::marker::PhantomData,
    wlan_bitfield::bitfield,
    zerocopy::{AsBytes, FromBytes},
};

// IEEE Std 802.11-2016, 9.2.4.1.3
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct FrameType(pub u8);

impl FrameType {
    pub const MGMT: Self = Self(0);
    pub const CTRL: Self = Self(1);
    pub const DATA: Self = Self(2);
    pub const EXT: Self = Self(3);
}

// IEEE Std 802.11-2016, 9.2.4.1.3
#[bitfield(
    0 cf_ack,
    1 cf_poll,
    2 null,
    3 qos,
    4..=7 _ // subtype is a 4-bit number
)]
#[derive(Clone, Copy, Hash, PartialEq, Eq)]
pub struct DataSubtype(pub u8);

// IEEE Std 802.11-2016, 9.2.4.1.3
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct MgmtSubtype(u8);

impl MgmtSubtype {
    pub const ASSOC_REQ: Self = Self(0b0000);
    pub const ASSOC_RESP: Self = Self(0b0001);
    pub const REASSOC_REQ: Self = Self(0b0010);
    pub const REASSOC_RESP: Self = Self(0b0011);
    pub const PROBE_REQ: Self = Self(0b0100);
    pub const PROBE_RESP: Self = Self(0b0101);
    pub const TIMING_AD: Self = Self(0b0110);
    // 0111 reserved
    pub const BEACON: Self = Self(0b1000);
    pub const ATIM: Self = Self(0b1001);
    pub const DISASSOC: Self = Self(0b1010);
    pub const AUTH: Self = Self(0b1011);
    pub const DEAUTH: Self = Self(0b1100);
    pub const ACTION: Self = Self(0b1101);
    pub const ACTION_NO_ACK: Self = Self(0b1110);
    // 1111 reserved
}

// IEEE Std 802.11-2016, 9.2.4.1.3
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct CtrlSubtype(u8);

impl CtrlSubtype {
    // 0000 - 0011 reserved
    pub const BEAM_FORMING: Self = Self(0b0100);
    pub const VHT_NDP_ANNOUNCE: Self = Self(0b0101);
    pub const CTRL_EXT: Self = Self(0b0110);
    pub const CTRL_WRAP: Self = Self(0b0111);
    pub const BLOCK_ACK: Self = Self(0b1000);
    pub const BLOCK_ACK_REQ: Self = Self(0b1001);
    pub const PS_POLL: Self = Self(0b1010);
    pub const RTS: Self = Self(0b1011);
    pub const CTS: Self = Self(0b1100);
    pub const ACK: Self = Self(0b1101);
    pub const CF_END: Self = Self(0b1110);
    pub const CF_END_ACK: Self = Self(0b1111);
}

/// The power management state of a station.
///
/// Represents the possible power states for doze BI from table 11-3 of IEEE-802.11-2016.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PowerState(bool);

impl PowerState {
    /// The awake power management state. When in this state, stations are expected to be reliable
    /// and handle transmitted frames.
    pub const AWAKE: Self = Self(false);
    /// The doze power management state. When in this state, stations may be less reliable and APs
    /// should typically buffer frames.
    pub const DOZE: Self = Self(true);
}

// IEEE Std 802.11-2016, 9.2.4.1.1
#[bitfield(
    0..=1   protocol_version,
    2..=3   frame_type as FrameType(u8),
    4..=7   union {
                frame_subtype,
                mgmt_subtype as MgmtSubtype(u8),
                data_subtype as DataSubtype(u8),
                ctrl_subtype as CtrlSubtype(u8),
            },
    8       to_ds,
    9       from_ds,
    10      more_fragments,
    11      retry,
    12      power_mgmt as PowerState(bool),
    13      more_data,
    14      protected,
    15      htc_order
)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy)]
#[repr(C)]
pub struct FrameControl(pub u16);

// IEEE Std 802.11-2016, 9.2.4.4
#[bitfield(
    0..=3   frag_num,
    4..=15  seq_num,
)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy)]
#[repr(C)]
pub struct SequenceControl(pub u16);

// IEEE Std 802.11-2016, 9.2.4.6
#[bitfield(
    0       vht,
    1..=29  middle, // see 9.2.4.6.2 for HT and 9.2.4.6.3 for VHT
    30      ac_constraint,
    31      rdg_more_ppdu,
)]
#[repr(C)]
#[derive(AsBytes, FromBytes, Copy, Clone, PartialEq, Eq)]
pub struct HtControl(pub u32);

#[derive(PartialEq, Eq)]
pub struct Presence<T: ?Sized>(bool, PhantomData<T>);

impl<T: ?Sized> Presence<T> {
    pub fn from_bool(present: bool) -> Self {
        Self(present, PhantomData)
    }
}

pub trait OptionalField {
    const PRESENT: Presence<Self> = Presence::<Self>(true, PhantomData);
    const ABSENT: Presence<Self> = Presence::<Self>(false, PhantomData);
}
impl<T: ?Sized> OptionalField for T {}
