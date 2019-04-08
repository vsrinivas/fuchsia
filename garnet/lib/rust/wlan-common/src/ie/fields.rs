// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer_reader::BufferReader, mac::MacAddr, mac::ReasonCode, unaligned_view::UnalignedView,
    },
    failure::{ensure, format_err, Error},
    std::mem::size_of,
    wlan_bitfield::bitfield,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

macro_rules! pub_const {
    ($name:ident, $val:expr) => {
        pub const $name: Self = Self($val);
    };
}

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

// IEEE Std 802.11-2016, 9.4.2.56
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
pub struct HtCapabilities {
    pub ht_cap_info: HtCapabilityInfo, // u16
    pub ampdu_params: AmpduParams,     // u8
    pub mcs_set: SupportedMcsSet,      // u128
    pub ht_ext_cap: HtExtCapabilities, // u16
    pub txbf_cap: TxBfCapability,      // u32
    pub asel_cap: AselCapability,      // u8
}

// IEEE Std 802.11-2016, 9.4.2.56.2
#[bitfield(
    0       ldpc_coding_cap,
    1..=1   chan_width_set as ChanWidthSet(u8), // In spec: Supported Channel Width Set
    2..=3   sm_power_save as SmPowerSave(u8),   // Spatial Multiplexing Power Save
    4       greenfield,                         // HT-Greenfield.
    5       short_gi_20,                        // Short Guard Interval for 20 MHz
    6       short_gi_40,                        // Short Guard Interval for 40 MHz
    7       tx_stbc,

    8..=9   rx_stbc,                            // maximum number of spatial streams. Up to 3.
    10      delayed_block_ack,                  // HT-delayed Block Ack
    11..=11 max_amsdu_len as MaxAmsduLen(u8),
    12      dsss_in_40,                         // DSSS/CCK Mode in 40 MHz
    13      _,                                  // reserved
    14      intolerant_40,                      // 40 MHz Intolerant
    15      lsig_txop_protect,
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtCapabilityInfo(pub u16);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct ChanWidthSet(pub u8);
impl ChanWidthSet {
    pub_const!(TWENTY_ONLY, 0);
    pub_const!(TWENTY_FORTY, 1);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct SmPowerSave(pub u8);
impl SmPowerSave {
    pub_const!(STATIC, 0);
    pub_const!(DYNAMIC, 1);
    // 2 reserved
    pub_const!(DISABLED, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct MaxAmsduLen(pub u8);
impl MaxAmsduLen {
    pub_const!(OCTETS3839, 0);
    pub_const!(OCTETS7935, 1);
}

// IEEE Std 802.11-2016, 9.4.2.56.3
#[bitfield(
    0..=1 exponent,                                     // Maximum A-MPDU Length Exponent.
    2..=4 min_start_spacing as MinMpduStartSpacing(u8), // Minimum MPDU Start Spacing.
    5..=7 _,                                            // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct AmpduParams(pub u8);

impl AmpduParams {
    pub fn max_ampdu_len(&self) -> usize {
        (1 << (13 + self.exponent())) - 1 as usize
    }
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct MinMpduStartSpacing(pub u8);
impl MinMpduStartSpacing {
    pub_const!(NO_RESTRICT, 0);
    pub_const!(QUATER_USEC, 1);
    pub_const!(HALF_USEC, 2);
    pub_const!(ONE_USEC, 3);
    pub_const!(TWO_USEC, 4);
    pub_const!(FOUR_USEC, 5);
    pub_const!(EIGHT_USEC, 6);
    pub_const!(SIXTEEN_USEC, 7);
}

// IEEE Std 802.11-2016, 9.4.2.56.4
// HT-MCS table in IEEE Std 802.11-2016, Annex B.4.17.2
// VHT-MCS tables in IEEE Std 802.11-2016, 21.5
#[bitfield(
    0..=76      rx_mcs as RxMcsBitmask(u128),
    77..=79     _,                                  // reserved
    80..=89     rx_highest_rate,                    // in Mbps
    90..=95     _,                                  // reserved

    96          tx_set_defined,
    97          tx_rx_diff,
    98..=99     tx_max_ss as NumSpatialStreams(u8),
    100         tx_ueqm,                            // Transmit Unequal Modulation.
    101..=127   _,                                  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct SupportedMcsSet(pub u128);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct RxMcsBitmask(pub u128);
impl RxMcsBitmask {
    pub fn support(&self, mcs_index: u8) -> bool {
        mcs_index <= 76 && (self.0 & (1 << mcs_index)) != 0
    }
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct NumSpatialStreams(u8);
impl NumSpatialStreams {
    // Value are "off-by-one" by definition. See IEEE 802.11-2016 Table 9-164
    pub_const!(ONE, 0);
    pub_const!(TWO, 1);
    pub_const!(THREE, 2);
    pub_const!(FOUR, 3);

    pub fn to_human(&self) -> u8 {
        1 + self.0
    }
    pub fn from_human(val: u8) -> Result<Self, Error> {
        ensure!(
            Self::ONE.to_human() <= val && val <= Self::FOUR.to_human(),
            format_err!("Number of spatial stream must be between 1 and 4. {} is invalid", val)
        );
        Ok(Self(val - 1))
    }
}

// IEEE Std 802.11-2016, 9.4.2.56.5
#[bitfield(
    0       pco,
    1..=2   pco_transition as PcoTransitionTime(u8),
    3..=7   _,                                          // reserved
    8..=9   mcs_feedback as McsFeedback(u8),
    10      htc_ht_support,
    11      rd_responder,
    12..=15 _,                                          // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtExtCapabilities(pub u16);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct PcoTransitionTime(pub u8);
impl PcoTransitionTime {
    pub_const!(PCO_RESERVED, 0); // Often translated as "No transition".
    pub_const!(PCO_400_USEC, 1);
    pub_const!(PCO_1500_USEC, 2);
    pub_const!(PCO_5000_USEC, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct McsFeedback(pub u8);
impl McsFeedback {
    pub_const!(NO_FEEDBACK, 0);
    // 1 reserved
    pub_const!(UNSOLICITED, 2);
    pub_const!(BOTH, 3);
}

// IEEE Std 802.11-2016, 9.4.2.56.6
#[bitfield(
    0       implicit_rx,
    1       rx_stag_sounding,
    2       tx_stag_sounding,
    3       rx_ndp,
    4       tx_ndp,
    5       implicit,
    6..=7   calibration as Calibration(u8),

    8       csi,                                // Explicit CSI Transmit Beamforming.

    9       noncomp_steering,                   // Explicit Noncompressed Steering
    10      comp_steering,                      // Explicit Compressed Steering
    11..=12 csi_feedback as Feedback(u8),
    13..=14 noncomp_feedback as Feedback(u8),
    15..=16 comp_feedback as Feedback(u8),
    17..=18 min_grouping as MinGroup(u8),
    19..=20 csi_antennas as NumAntennas(u8),

    21..=22 noncomp_steering_ants as NumAntennas(u8),
    23..=24 comp_steering_ants as NumAntennas(u8),
    25..=26 csi_rows as NumCsiRows(u8),
    27..=28 chan_estimation as NumSpaceTimeStreams(u8),
    29..=31 _,                                  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct TxBfCapability(pub u32);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct Calibration(pub u8);
impl Calibration {
    pub_const!(NONE, 0);
    pub_const!(RESPOND_NO_INITIATE, 1);
    // 2 Reserved
    pub_const!(RESPOND_INITIATE, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct Feedback(pub u8);
impl Feedback {
    pub_const!(NONE, 0);
    pub_const!(DELAYED, 1);
    pub_const!(IMMEDIATE, 2);
    pub_const!(DELAYED_IMMEDIATE, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct MinGroup(pub u8);
impl MinGroup {
    pub_const!(ONE, 0); // Meaning no grouping
    pub_const!(TWO, 1);
    pub_const!(FOUR, 2);
    pub_const!(TWO_FOUR, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct NumAntennas(u8);
impl NumAntennas {
    // Value are "off-by-one" by definition. See IEEE 802.11-2016 Table 9-166
    pub_const!(ONE, 0);
    pub_const!(TWO, 1);
    pub_const!(THREE, 2);
    pub_const!(FOUR, 3);

    pub fn to_human(&self) -> u8 {
        1 + self.0
    }
    pub fn from_human(val: u8) -> Result<Self, Error> {
        ensure!(
            Self::ONE.to_human() <= val && val <= Self::FOUR.to_human(),
            format_err!("Number of antennas must be between 1 and 4. {} is invalid", val)
        );
        Ok(Self(val - 1))
    }
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct NumCsiRows(u8);
impl NumCsiRows {
    // Value are "off-by-one" by definition. See IEEE 802.11-2016 Table 9-166
    pub_const!(ONE, 0);
    pub_const!(TWO, 1);
    pub_const!(THREE, 2);
    pub_const!(FOUR, 3);

    pub fn to_human(&self) -> u8 {
        1 + self.0
    }
    pub fn from_human(val: u8) -> Result<Self, Error> {
        ensure!(
            Self::ONE.to_human() <= val && val <= Self::FOUR.to_human(),
            format_err!("Number of csi rows must be between 1 and 4. {} is invalid", val)
        );
        Ok(Self(val - 1))
    }
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct NumSpaceTimeStreams(u8);
impl NumSpaceTimeStreams {
    // Value are "off-by-one" by definition. See IEEE 802.11-2016 Table 9-166
    pub_const!(ONE, 0);
    pub_const!(TWO, 1);
    pub_const!(THREE, 2);
    pub_const!(FOUR, 3);

    pub fn to_human(&self) -> u8 {
        1 + self.0
    }
    pub fn from_human(val: u8) -> Result<Self, Error> {
        ensure!(
            1 <= val && val <= 4,
            format_err!("Number of channel estimation must be between 1 and 4. {} is invalid", val)
        );
        Ok(Self(val - 1))
    }
}

// IEEE Std 802.11-2016, 9.4.2.56.6
#[bitfield(
    0 asel,
    1 csi_feedback_tx_asel,     // Explicit CSI Feedback based Transmit ASEL
    2 ant_idx_feedback_tx_asel,
    3 explicit_csi_feedback,
    4 antenna_idx_feedback,
    5 rx_asel,
    6 tx_sounding_ppdu,
    7 _,                        // reserved,
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct AselCapability(pub u8);

// IEEE Std 802.11-2016, 9.4.2.57
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
pub struct HtOperation {
    pub primary_chan: u8, // Primary 20 MHz channel.
    // HT Operation Information is 40-bit field so it has to be split
    pub ht_op_info_head: HtOpInfoHead,     // u8
    pub ht_op_info_tail: HtOpInfoTail,     // u32
    pub basic_ht_mcs_set: SupportedMcsSet, // u128
}

// IEEE Std 802.11-2016, Figure 9-339
#[bitfield(
    0..=1 secondary_chan_offset as SecChanOffset(u8),
    2..=2 sta_chan_width as StaChanWidth(u8),
    3     rifs_mode_permitted,
    4..=7 _,    // reserved. Note: used by 802.11n-D1.10 (before 802.11n-2009)
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtOpInfoHead(pub u8);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct SecChanOffset(pub u8);
impl SecChanOffset {
    pub_const!(SECONDARY_NONE, 0); // No secondary channel
    pub_const!(SECONDARY_ABOVE, 1); // Secondary channel is above the primary channel
                                    // 2 reserved
    pub_const!(SECONDARY_BELOW, 3); // Secondary channel is below the primary channel
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct StaChanWidth(pub u8);
impl StaChanWidth {
    pub_const!(TWENTY_MHZ, 0);
    pub_const!(ANY, 1); // Any in the Supported Channel Width set
}

// IEEE Std 802.11-2016, Figure 9-339, continued
#[bitfield(
    // bit offset in this struct starts from bit 8 in the IEEE HtOperationInformation field.
    0..=1   ht_protection as HtProtection(u8),
    2       nongreenfield_present,
    3       _,                                  // reserved. Note: used in 802.11n-D1.10
                                                // (before 802.11n-2009).
    4       obss_non_ht_stas_present,
    // IEEE 802.11-2016 Figure 9-339 has an inconsistency so this is Fuchsia interpretation:
    // The channel number for the second segment in a 80+80 Mhz channel
    5..=12  center_freq_seg2,                   // For VHT only. See Table 9-250
    13..=15 _,                                  // reserved

    16..=21 _,                                  // reserved
    22      dual_beacon,                        // whether an STBC beacon is transmitted by the AP
    23      dual_cts_protection,                // whether CTS protection is required
    24      stbc_beacon,                        // 0 indicates primary beacon, 1 STBC beacon
    25      lsig_txop_protection,               // only true if all HT STAs in the BSS support this
    26      pco_active,
    27..=27 pco_phase as PcoPhase(u8),
    28..=31 _,                                  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtOpInfoTail(pub u32);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct HtProtection(pub u8);
impl HtProtection {
    pub_const!(NONE, 0);
    pub_const!(NON_MEMBER, 1);
    pub_const!(TWENTY_MHZ, 2);
    pub_const!(NON_HT_MIXED, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct PcoPhase(pub u8);
impl PcoPhase {
    pub_const!(TWENTY_MHZ, 0);
    pub_const!(FORTY_MHZ, 1);
}

#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, Debug, AsBytes, FromBytes)]
pub struct MpmProtocol(pub u16);

// IEEE Std 802.11-2016, 9.4.2.102, table 9-222
impl MpmProtocol {
    pub_const!(MPM, 0);
    pub_const!(AMPE, 1);
    // 2-254 reserved
    pub_const!(VENDOR_SPECIFIC, 255);
    // 255-65535 reserved
}

// IEEE Std 802.11-2016, 9.4.2.102
// The fixed part of the Mesh Peering Management header
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct MpmHeader {
    pub protocol: MpmProtocol,
    pub local_link_id: u16,
}

// IEEE Std 802.11-2016, 9.4.2.102
// The optional "PMK" part of the MPM element
#[repr(C)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct MpmPmk(pub [u8; 16]);

// MPM element in a "mesh peering open" frame
pub struct MpmOpenView<B> {
    pub header: LayoutVerified<B, MpmHeader>,
    pub pmk: Option<LayoutVerified<B, MpmPmk>>,
}

// MPM element in a "mesh peering confirm" frame
pub struct MpmConfirmView<B> {
    pub header: LayoutVerified<B, MpmHeader>,
    pub peer_link_id: UnalignedView<B, u16>,
    pub pmk: Option<LayoutVerified<B, MpmPmk>>,
}

// MPM element in a "mesh peering close" frame
pub struct MpmCloseView<B> {
    pub header: LayoutVerified<B, MpmHeader>,
    pub peer_link_id: Option<UnalignedView<B, u16>>,
    pub reason_code: UnalignedView<B, ReasonCode>,
    pub pmk: Option<LayoutVerified<B, MpmPmk>>,
}

// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-478
#[bitfield(
    0       gate_announcement,
    1       addressing_mode,
    2       proactive,
    3..=5   _, // reserved
    6       addr_ext,
    7       _, // reserved
)]
#[repr(C)]
#[derive(Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct PreqFlags(pub u8);

// Fixed-length fields of the PREQ element that precede
// the optional Originator External Address field.
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PreqHeader {
    pub flags: PreqFlags,
    pub hop_count: u8,
    pub element_ttl: u8,
    pub path_discovery_id: u32,
    pub originator_addr: MacAddr,
    pub originator_hwmp_seqno: u32,
}

// Fixed-length fields of the PREQ elements that follow the optional Originator External Address
// field and precede the variable length per-target fields.
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PreqMiddle {
    pub lifetime: u32,
    pub metric: u32,
    pub target_count: u8,
}

// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-479
#[bitfield(
    0       target_only,
    1       _, // reserved
    2       usn,
    3..=7   _, // reserved
)]
#[repr(C)]
#[derive(Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct PreqPerTargetFlags(pub u8);

// An entry of the variable-length part of PREQ
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PreqPerTarget {
    pub flags: PreqPerTargetFlags,
    pub target_addr: MacAddr,
    pub target_hwmp_seqno: u32,
}

pub struct PreqView<B> {
    pub header: LayoutVerified<B, PreqHeader>,
    pub originator_external_addr: Option<LayoutVerified<B, MacAddr>>,
    pub middle: LayoutVerified<B, PreqMiddle>,
    pub targets: LayoutVerified<B, [PreqPerTarget]>,
}

// Fixed-length fields of the PERR element that precede the variable-length
// per-destination fields.
// IEEE Std 802.11-2016, 9.4.2.115
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PerrHeader {
    pub element_ttl: u8,
    pub num_destinations: u8,
}

// IEEE Std 802.11-2016, 9.4.2.115, Figure 9-483
#[bitfield(
    0..=5   _, // reserved
    6       addr_ext,
    7       _, // reserved
)]
#[repr(C)]
#[derive(Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct PerrDestinationFlags(pub u8);

// Fixed-length fields of the per-destination chunk of the PERR element
// that precede the optional "Destination External Address" field.
// IEEE Std 802.11-2016, 9.4.2.115
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PerrDestinationHeader {
    pub flags: PerrDestinationFlags,
    pub dest_addr: MacAddr,
    pub hwmp_seqno: u32,
}

pub struct PerrDestinationView<B> {
    pub header: LayoutVerified<B, PerrDestinationHeader>,
    pub ext_addr: Option<LayoutVerified<B, MacAddr>>,
    pub reason_code: UnalignedView<B, ReasonCode>,
}

pub struct PerrView<B> {
    pub header: LayoutVerified<B, PerrHeader>,
    pub destinations: PerrDestinationListView<B>,
}

pub struct PerrDestinationListView<B>(pub B);

impl<B: ByteSlice> IntoIterator for PerrDestinationListView<B> {
    type Item = PerrDestinationView<B>;
    type IntoIter = PerrDestinationIter<B>;

    fn into_iter(self) -> Self::IntoIter {
        PerrDestinationIter(BufferReader::new(self.0))
    }
}

impl<'a, B: ByteSlice> IntoIterator for &'a PerrDestinationListView<B> {
    type Item = PerrDestinationView<&'a [u8]>;
    type IntoIter = PerrDestinationIter<&'a [u8]>;

    fn into_iter(self) -> Self::IntoIter {
        PerrDestinationIter(BufferReader::new(&self.0[..]))
    }
}

impl<B: ByteSlice> PerrDestinationListView<B> {
    pub fn iter(&self) -> PerrDestinationIter<&[u8]> {
        self.into_iter()
    }
}

pub struct PerrDestinationIter<B>(BufferReader<B>);

impl<B: ByteSlice> Iterator for PerrDestinationIter<B> {
    type Item = PerrDestinationView<B>;

    fn next(&mut self) -> Option<Self::Item> {
        let have_ext_addr = self.0.peek::<PerrDestinationHeader>()?.flags.addr_ext();
        let dest_len = size_of::<PerrDestinationHeader>()
            + if have_ext_addr { size_of::<MacAddr>() } else { 0 }
            + size_of::<ReasonCode>();
        if self.0.bytes_remaining() < dest_len {
            None
        } else {
            // Unwraps are OK because we checked the length above
            let header = self.0.read().unwrap();
            let ext_addr = if have_ext_addr { Some(self.0.read().unwrap()) } else { None };
            let reason_code = self.0.read_unaligned().unwrap();
            Some(PerrDestinationView { header, ext_addr, reason_code })
        }
    }
}

impl<B: ByteSlice> PerrDestinationIter<B> {
    pub fn bytes_remaining(&self) -> usize {
        self.0.bytes_remaining()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn perr_iter_empty() {
        let empty: [u8; 0] = [];
        let mut iter = PerrDestinationListView(&empty[..]).into_iter();
        assert!(iter.next().is_none());
        assert_eq!(0, iter.bytes_remaining());
    }

    #[test]
    fn perr_iter_two_destinations() {
        #[rustfmt::skip]
        let data = [
            // Destination 1
            0x40, // flags: address extension
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
            0x11, 0x22, 0x33, 0x44, // HWMP seqno
            0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a,  // ext addr
            0x55, 0x66, // reason code
            // Destination 2
            0, // flags
            0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, // dest addr
            0x77, 0x88, 0x99, 0xaa, // HWMP seqno
            0xbb, 0xcc, // reason code
        ];
        let mut iter = PerrDestinationListView(&data[..]).into_iter();
        assert!(iter.bytes_remaining() > 0);

        {
            let target = iter.next().expect("expected first target");
            assert_eq!(0x44332211, { target.header.hwmp_seqno });
            let ext_addr = target.ext_addr.expect("expected external addr");
            assert_eq!([0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a], *ext_addr);
            assert_eq!(0x6655, target.reason_code.get().0);
        }

        assert!(iter.bytes_remaining() > 0);

        {
            let target = iter.next().expect("expected second target");
            assert_eq!(0xaa998877, { target.header.hwmp_seqno });
            assert!(target.ext_addr.is_none());
            assert_eq!(0xccbb, target.reason_code.get().0);
        }

        assert_eq!(0, iter.bytes_remaining());
        assert!(iter.next().is_none());
        assert_eq!(0, iter.bytes_remaining());
    }

    #[test]
    fn perr_iter_too_short_for_header() {
        #[rustfmt::skip]
        let data = [
            0x00, // flags: no address extension
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
            0x11, 0x22, 0x33, // one byte missing from HWMP seqno
        ];
        let mut iter = PerrDestinationListView(&data[..]).into_iter();
        assert_eq!(data.len(), iter.bytes_remaining());
        assert!(iter.next().is_none());
        assert_eq!(data.len(), iter.bytes_remaining());
    }

    #[test]
    fn perr_iter_too_short_for_ext_addr() {
        #[rustfmt::skip]
        let data = [
            // Destination 1
            0x40, // flags: address extension
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
            0x11, 0x22, 0x33, 0x44, // HWMP seqno
            0x1a, 0x2a, 0x3a, 0x4a, 0x5a, // one byte missing from ext addr
        ];
        let mut iter = PerrDestinationListView(&data[..]).into_iter();
        assert_eq!(data.len(), iter.bytes_remaining());
        assert!(iter.next().is_none());
        assert_eq!(data.len(), iter.bytes_remaining());
    }

    #[test]
    fn perr_iter_too_short_for_reason_code() {
        #[rustfmt::skip]
        let data = [
            // Target 1
            0x40, // flags: address extension
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
            0x11, 0x22, 0x33, 0x44, // HWMP seqno
            0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a,  // ext addr
            0x55, // one byte missing from the reason code
        ];
        let mut iter = PerrDestinationListView(&data[..]).into_iter();
        assert_eq!(data.len(), iter.bytes_remaining());
        assert!(iter.next().is_none());
        assert_eq!(data.len(), iter.bytes_remaining());
    }
}
