// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer_reader::BufferReader, mac::MacAddr, mac::ReasonCode, organization::Oui,
        unaligned_view::UnalignedView,
    },
    banjo_ddk_hw_wlan_ieee80211 as banjo_80211, banjo_ddk_protocol_wlan_info as banjo_wlan_info,
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
#[derive(FromBytes, AsBytes, Unaligned, Clone, Copy)]
#[repr(C, packed)]
pub struct TimHeader {
    pub dtim_count: u8,
    pub dtim_period: u8,
    pub bmp_ctrl: BitmapControl,
}

pub struct TimView<B> {
    pub header: TimHeader,
    pub bitmap: B,
}

// WFA WMM v1.2, 2.2.1
#[bitfield(
    0..=7   union {
                client_wmm_info as ClientWmmInfo(u8),
                ap_wmm_info as ApWmmInfo(u8),
            }
)]
#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct WmmInfo(pub u8);

// WFA WMM v1.2, 2.2.1 Figure 6
#[bitfield(
    0..=3   parameter_set_count,
    4..=6   _, // reserved
    7       uapsd
)]
#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct ApWmmInfo(pub u8);

// WFA WMM v1.2, 2.2.1 Figure 7
#[bitfield(
    0       ac_vo_uapsd,
    1       ac_vi_uapsd,
    2       ac_bk_uapsd,
    3       ac_be_uapsd,
    4       _, // reserved
    5..=6   max_sp_length,
    7       _  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct ClientWmmInfo(pub u8);

// WFA WMM v1.2, 2.2.2 Table 5
#[repr(C, packed)]
#[derive(PartialEq, Eq, Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct WmmParam {
    pub wmm_info: WmmInfo,
    pub _reserved: u8,
    pub ac_be_params: WmmAcParams,
    pub ac_bk_params: WmmAcParams,
    pub ac_vi_params: WmmAcParams,
    pub ac_vo_params: WmmAcParams,
}

// WFA WMM v1.2, 2.2.2 Figure 9
#[repr(C, packed)]
#[derive(PartialEq, Eq, Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct WmmAcParams {
    pub aci_aifsn: WmmAciAifsn,
    pub ecw_min_max: EcwMinMax,
    /// unit of 32 microseconds
    pub txop_limit: u16,
}

// WFA WMM v1.2, 2.2.2 Figure 10
#[bitfield(
    0..=3   aifsn,
    4       acm,
    5..=6   aci,
    7       _  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct WmmAciAifsn(pub u8);

// WFA WMM v1.2, 2.2.2 Figure 11
#[bitfield(
    0..=3   ecw_min,
    4..=7   ecw_max,
)]
#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct EcwMinMax(pub u8);

// IEEE Std 802.11-2016, 9.4.2.9
pub struct CountryView<B> {
    pub country_code: [u8; 2],
    pub environment: CountryEnvironment,
    // the rest are unparsed currently
    pub subbands: B,
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct CountryEnvironment(pub u8);

impl CountryEnvironment {
    pub const INDOOR: Self = Self(b'I');
    pub const OUTDOOR: Self = Self(b'O');
    pub const ANY: Self = Self(b' ');
}

// IEEE Std 802.11-2016, 9.4.2.56
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy, Debug)]
pub struct HtCapabilities {
    pub ht_cap_info: HtCapabilityInfo, // u16
    pub ampdu_params: AmpduParams,     // u8
    pub mcs_set: SupportedMcsSet,      // u128
    pub ht_ext_cap: HtExtCapabilities, // u16
    pub txbf_cap: TxBfCapability,      // u32
    pub asel_cap: AselCapability,      // u8
}

impl From<banjo_80211::Ieee80211HtCapabilities> for HtCapabilities {
    fn from(cap: banjo_80211::Ieee80211HtCapabilities) -> Self {
        // Safe to access `fields` because its value is read as raw bytes
        let sms = unsafe { cap.supported_mcs_set.fields };
        let mcs_set = (sms.rx_mcs_head as u128)
            | (sms.rx_mcs_tail as u128) << 64
            | (sms.tx_mcs as u128) << 96;
        Self {
            ht_cap_info: HtCapabilityInfo(cap.ht_capability_info),
            ampdu_params: AmpduParams(cap.ampdu_params),
            mcs_set: SupportedMcsSet(mcs_set),
            ht_ext_cap: HtExtCapabilities(cap.ht_ext_capabilities),
            txbf_cap: TxBfCapability(cap.tx_beamforming_capabilities),
            asel_cap: AselCapability(cap.asel_capabilities),
        }
    }
}

impl From<HtCapabilities> for banjo_80211::Ieee80211HtCapabilities {
    fn from(cap: HtCapabilities) -> Self {
        Self {
            ht_capability_info: *&{ cap.ht_cap_info }.raw(),
            ampdu_params: *&{ cap.ampdu_params }.raw(),
            supported_mcs_set: cap.mcs_set.into(),
            ht_ext_capabilities: *&{ cap.ht_ext_cap }.raw(),
            tx_beamforming_capabilities: *&{ cap.txbf_cap }.raw(),
            asel_capabilities: *&{ cap.asel_cap }.raw(),
        }
    }
}

impl From<SupportedMcsSet> for banjo_80211::Ieee80211HtCapabilitiesSupportedMcsSet {
    fn from(mcs: SupportedMcsSet) -> Self {
        Self {
            fields: banjo_80211::Ieee80211HtCapabilitiesSupportedMcsSetFields {
                rx_mcs_head: (mcs.raw() & ((1 << 64) - 1)) as u64,
                rx_mcs_tail: ((mcs.raw() >> 64) & ((1 << 32) - 1)) as u32,
                tx_mcs: ((mcs.raw() >> 96) & ((1 << 32) - 1)) as u32,
            },
        }
    }
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
    pub_const!(OCTETS_3839, 0);
    pub_const!(OCTETS_7935, 1);
}

// IEEE Std 802.11-2016, 9.4.2.56.3
#[bitfield(
    0..=1 max_ampdu_exponent as MaxAmpduExponent(u8),   // Maximum A-MPDU Length Exponent. 0-3 valid
    2..=4 min_start_spacing as MinMpduStartSpacing(u8), // Minimum MPDU Start Spacing.
    5..=7 _,                                            // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct AmpduParams(pub u8);

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct MaxAmpduExponent(pub u8);
impl MaxAmpduExponent {
    pub fn to_len(&self) -> usize {
        (1 << (13 + self.0)) - 1 as usize
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
    pub fn from_human(val: u8) -> Result<Self, String> {
        if Self::ONE.to_human() <= val && val <= Self::FOUR.to_human() {
            Ok(Self(val - 1))
        } else {
            Err(format!("Number of spatial stream must be between 1 and 4. {} is invalid", val))
        }
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
    pub fn from_human(val: u8) -> Result<Self, String> {
        if Self::ONE.to_human() <= val && val <= Self::FOUR.to_human() {
            Ok(Self(val - 1))
        } else {
            Err(format!("Number of antennas must be between 1 and 4. {} is invalid", val))
        }
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
    pub fn from_human(val: u8) -> Result<Self, String> {
        if Self::ONE.to_human() <= val && val <= Self::FOUR.to_human() {
            Ok(Self(val - 1))
        } else {
            Err(format!("Number of csi rows must be between 1 and 4. {} is invalid", val))
        }
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
    pub fn from_human(val: u8) -> Result<Self, String> {
        if 1 <= val && val <= 4 {
            Ok(Self(val - 1))
        } else {
            Err(format!("Number of channel estimation must be between 1 and 4. {} is invalid", val))
        }
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
    pub ht_op_info_head: HtOpInfoHead,     // u32
    pub ht_op_info_tail: HtOpInfoTail,     // u8
    pub basic_ht_mcs_set: SupportedMcsSet, // u128
}

impl From<HtOperation> for banjo_wlan_info::WlanHtOp {
    fn from(op: HtOperation) -> Self {
        let mcs = banjo_80211::Ieee80211HtCapabilitiesSupportedMcsSet::from(op.basic_ht_mcs_set);
        Self {
            primary_chan: op.primary_chan,
            head: *&{ op.ht_op_info_head }.raw(),
            tail: *&{ op.ht_op_info_tail }.raw(),
            rx_mcs_head: unsafe { mcs.fields.rx_mcs_head },
            rx_mcs_tail: unsafe { mcs.fields.rx_mcs_tail },
            tx_mcs: unsafe { mcs.fields.tx_mcs },
        }
    }
}

// TODO(43257): Move bits 8-32 into tail.
// IEEE Std 802.11-2016, Figure 9-339
#[bitfield(
    0..=1 secondary_chan_offset as SecChanOffset(u8),
    2..=2 sta_chan_width as StaChanWidth(u8),
    3     rifs_mode_permitted,
    4..=7 _,    // reserved. Note: used by 802.11n-D1.10 (before 802.11n-2009)

    8..=9   ht_protection as HtProtection(u8),
    10      nongreenfield_present,
    11      _,                                  // reserved. Note: used in 802.11n-D1.10
                                                // (before 802.11n-2009).
    12      obss_non_ht_stas_present,
    // IEEE 802.11-2016 Figure 9-339 has an inconsistency so this is Fuchsia interpretation:
    // The channel number for the second segment in a 80+80 Mhz channel
    13..=20 center_freq_seg2,                   // For VHT only. See Table 9-250
    21..=23 _,                                  // reserved
    24..=29 _,                                  // reserved
    30      dual_beacon,                        // whether an STBC beacon is transmitted by the AP
    31      dual_cts_protection,                // whether CTS protection is required
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtOpInfoHead(pub u32);

#[derive(Debug, PartialOrd, PartialEq, Eq, Clone, Copy)]
pub struct SecChanOffset(pub u8);
impl SecChanOffset {
    pub_const!(SECONDARY_NONE, 0); // No secondary channel
    pub_const!(SECONDARY_ABOVE, 1); // Secondary channel is above the primary channel
                                    // 2 reserved
    pub_const!(SECONDARY_BELOW, 3); // Secondary channel is below the primary channel
}

#[derive(Debug, PartialOrd, PartialEq, Eq, Clone, Copy)]
pub struct StaChanWidth(pub u8);
impl StaChanWidth {
    pub_const!(TWENTY_MHZ, 0);
    pub_const!(ANY, 1); // Any in the Supported Channel Width set
}

// TODO(43257): Move bits 8-32 from head into here.
// IEEE Std 802.11-2016, Figure 9-339, continued
#[bitfield(
    // bit offset in this struct starts from bit 32 in the IEEE HtOperationInformation field.
    0     stbc_beacon,                        // 0 indicates primary beacon, 1 STBC beacon
    1     lsig_txop_protection,               // only true if all HT STAs in the BSS support this
    2     pco_active,
    3..=3 pco_phase as PcoPhase(u8),
    4..=7 _,                                  // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct HtOpInfoTail(pub u8);

#[derive(Debug, PartialOrd, PartialEq, Eq, Clone, Copy)]
pub struct HtProtection(pub u8);
impl HtProtection {
    pub_const!(NONE, 0);
    pub_const!(NON_MEMBER, 1);
    pub_const!(TWENTY_MHZ, 2);
    pub_const!(NON_HT_MIXED, 3);
}

#[derive(Debug, PartialOrd, PartialEq, Eq, Clone, Copy)]
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

// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-481
#[bitfield(
    0..=5   _, // reserved
    6       addr_ext,
    7       _, // reserved
)]
#[repr(C)]
#[derive(Clone, Copy, AsBytes, FromBytes, Unaligned)]
pub struct PrepFlags(pub u8);

// Fixed-length fields of the PREP element that precede
// the optional Target External Address field.
// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-480
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PrepHeader {
    pub flags: PrepFlags,
    pub hop_count: u8,
    pub element_ttl: u8,
    pub target_addr: MacAddr,
    pub target_hwmp_seqno: u32,
}

// Fixed-length fields of the PREP element that follow
// the optional Target External Address field.
// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-480
#[repr(C, packed)]
#[derive(Clone, Copy, Debug, AsBytes, FromBytes, Unaligned)]
pub struct PrepTail {
    pub lifetime: u32,
    pub metric: u32,
    pub originator_addr: MacAddr,
    pub originator_hwmp_seqno: u32,
}

pub struct PrepView<B> {
    pub header: LayoutVerified<B, PrepHeader>,
    pub target_external_addr: Option<LayoutVerified<B, MacAddr>>,
    pub tail: LayoutVerified<B, PrepTail>,
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

// This enum represents all vendor IEs we know how to parse, plus an Unknown option for all other
// vendor IEs.
#[derive(Debug)]
pub enum VendorIe<B: ByteSlice> {
    // This does not contain the first byte of the IE body, since this byte identifies the IE as
    // WPA rather than another MSFT vendor IE.
    MsftLegacyWpa(B),
    // WiFi Simple Configuration element.
    // Like WPA, this does not contain the bytes identifying the IE as WSC.
    Wsc(B),
    // WMM Info is a single byte. The IE header and the IE body's first six bytes
    // (OUI, OUI type, OUI subtype, and version) are stripped.
    WmmInfo(B),
    // This does not contain the IE body's first six bytes
    // (OUI, OUI type, OUI subtype, and version) that identify IE as WMM Parameter
    WmmParam(B),
    // IEEE Std 802.11-2016, 9.4.2.26
    Unknown { oui: Oui, body: B },
}

// IEEE Std 802.11-2016, 9.4.2.57
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy, Debug)]
pub struct VhtCapabilities {
    pub vht_cap_info: VhtCapabilitiesInfo, // u32
    pub vht_mcs_nss: VhtMcsNssSet,         // u64
}

impl From<banjo_80211::Ieee80211VhtCapabilities> for VhtCapabilities {
    fn from(cap: banjo_80211::Ieee80211VhtCapabilities) -> Self {
        Self {
            vht_cap_info: VhtCapabilitiesInfo(cap.vht_capability_info),
            vht_mcs_nss: VhtMcsNssSet(cap.supported_vht_mcs_and_nss_set),
        }
    }
}

impl From<VhtCapabilities> for banjo_80211::Ieee80211VhtCapabilities {
    fn from(cap: VhtCapabilities) -> Self {
        Self {
            vht_capability_info: *&{ cap.vht_cap_info }.raw(),
            supported_vht_mcs_and_nss_set: *&{ cap.vht_mcs_nss }.raw(),
        }
    }
}

// IEEE Std 802.11-2016, 9.4.2.158.2
#[bitfield(
    0..=1   max_mpdu_len as MaxMpduLen(u8),
    2..=3   supported_cbw_set,                          // used with ext_nss_bw, See Table 9-250.
    4       rx_ldpc,
    5       sgi_cbw80,                                  // for CBW80 only
    6       sgi_cbw160,                                 // for CBW160 and CBW80P80
    7       tx_stbc,
    8..=10  rx_stbc,
    11      su_bfer,                                    // single user beamformer capable
    12      su_bfee,                                    // single user beamformee capable
    13..=15 bfee_sts,                                   // beamformee space-time spreading
                                                        // capability

    16..=18 num_sounding,                               // number of sounding dimensions
    19      mu_bfer,                                    // multi user beamformer capable
    20      mu_bfee,                                    // multi user beamformer capable
    21      txop_ps,                                    // TXOP power save mode
    22      htc_vht,
    23..=25 max_ampdu_exponent as MaxAmpduExponent(u8), // valid values: 0-7
    26..=27 link_adapt as VhtLinkAdaptation(u8),        // VHT link adapatation capable,
                                                        // only valid if htc_vht is true
    28      rx_ant_pattern,
    29      tx_ant_pattern,
    30..=31 ext_nss_bw,                                 // Extended NSS BW support, used with
                                                        // supported_cbw_set to indicate NSS support
                                                        // for each BW. See Table 9-250.
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct VhtCapabilitiesInfo(pub u32);

// IEEE Std 802.11-2016, 9.4.2.79
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
pub struct BssMaxIdlePeriod {
    // dot11BssMaxIdlePeriod (IEEE Std 802.11-2016, 11.24.13 and Annex C.3) is measured in
    // increments of 1000 TUs, with a range from 1-65535.
    pub max_idle_period: u16,
    pub idle_options: IdleOptions,
}

// IEEE Std 802.11-2016, 9.4.2.158.2
#[bitfield(
    0     protected_keep_alive_required, // Set to 1 to indicate only a protected frame indicates
                                         // activity
    1..=7 _, // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct IdleOptions(pub u8);

// IEEE Std 802.11-2016, Table 9-249
#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct MaxMpduLen(pub u8);
impl MaxMpduLen {
    pub_const!(OCTECTS_3895, 0);
    pub_const!(OCTECTS_7991, 1);
    pub_const!(OCTECTS_11454, 2);
    // 3 reserved
}

// IEEE Std 802.11-2016, Table 9-249
#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct VhtLinkAdaptation(pub u8);
impl VhtLinkAdaptation {
    pub_const!(NO_FEEDBACK, 0);
    // 1 Reserved
    pub_const!(UNSOLICITED, 2);
    pub_const!(BOTH, 3);
}

// IEEE Std 802.11-2016, 9.4.2.158.3
#[bitfield(
    0..=15  rx_max_mcs as VhtMcsNssMap(u16),

    16..=28 rx_max_data_rate,               // Mbps rounded down to the nearest integer
    29..=31 max_nsts,

    32..=47 tx_max_mcs as VhtMcsNssMap(u16),

    48..=60 tx_max_data_rate,               // Mbps rounded down to the nearest integer
    61      ext_nss_bw,                     // Extended NSS BW Capable
    62..=63 _,                              // reserved
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct VhtMcsNssSet(pub u64);

// IEEE Std 802.11-2016, Figure 9-562.
#[bitfield(
    0..=1   ss1 as VhtMcsSet(u8),
    2..=3   ss2 as VhtMcsSet(u8),
    4..=5   ss3 as VhtMcsSet(u8),
    6..=7   ss4 as VhtMcsSet(u8),
    8..=9   ss5 as VhtMcsSet(u8),
    10..=11 ss6 as VhtMcsSet(u8),
    12..=13 ss7 as VhtMcsSet(u8),
    14..=15 ss8 as VhtMcsSet(u8),
)]
#[repr(C)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct VhtMcsNssMap(pub u16);
impl VhtMcsNssMap {
    const BIT_WIDTH: u8 = 2;
    const MASK: u16 = (1 << Self::BIT_WIDTH) - 1;

    pub fn ss(&self, num: u8) -> Result<VhtMcsSet, String> {
        if num >= 1 && num <= 8 {
            Ok(VhtMcsSet((self.0 >> ((num - 1) * Self::BIT_WIDTH) & Self::MASK) as u8))
        } else {
            Err(format!("spatial stream number must be between 1 and 8, {} invalid", num))
        }
    }

    pub fn set_ss(&mut self, num: u8, val: VhtMcsSet) -> Result<(), String> {
        if num == 0 || num > 8 {
            Err(format!("spatial stream number must be between 1 and 8, {} invalid", num))
        } else if val.0 > 3 {
            Err(format!("bitfield is only 2 bit wide, {} invalid", val.0))
        } else {
            let offset = (num - 1) * Self::BIT_WIDTH;
            let mask = Self::MASK << offset;
            self.0 = (self.0 & (!mask)) | (((val.0 as u16) & Self::MASK) << offset);
            Ok(())
        }
    }
}

#[derive(Debug, PartialOrd, PartialEq, Clone, Copy)]
pub struct VhtMcsSet(pub u8);
impl VhtMcsSet {
    pub_const!(UP_TO_7, 0);
    pub_const!(UP_TO_8, 1);
    pub_const!(UP_TO_9, 2);
    pub_const!(NONE, 3);
}

// IEEE Std 802.11-2016, 9.4.2.159
#[repr(C, packed)]
#[derive(PartialEq, Eq, Hash, AsBytes, FromBytes, Unaligned, Clone, Copy)]
// TODO(fxbug.dev/29669): Derive phy parameters based on Table 9-250 and 9-253.
pub struct VhtOperation {
    pub vht_cbw: VhtChannelBandwidth, // u8
    pub center_freq_seg0: u8,         // Channel index
    pub center_freq_seg1: u8,         // Channel index

    pub basic_mcs_nss: VhtMcsNssMap, // u16
}

impl From<VhtOperation> for banjo_wlan_info::WlanVhtOp {
    fn from(op: VhtOperation) -> Self {
        Self {
            // vht_cbw is a NewType for u8 instead of a bitfield, thus does not have raw() defined.
            vht_cbw: op.vht_cbw.0,
            center_freq_seg0: op.center_freq_seg0,
            center_freq_seg1: op.center_freq_seg1,
            basic_mcs: *&{ op.basic_mcs_nss }.raw(),
        }
    }
}

// IEEE Std 802.11-2016, Table 9-252
#[repr(C)]
#[derive(Debug, PartialOrd, PartialEq, Eq, Hash, AsBytes, FromBytes, Clone, Copy)]
pub struct VhtChannelBandwidth(pub u8);
impl VhtChannelBandwidth {
    pub_const!(CBW_20_40, 0);
    pub_const!(CBW_80_160_80P80, 1);
    pub_const!(CBW_160, 2); // deprecated
    pub_const!(CBW_80P80, 3); // deprecated
                              // 4-255 reserved
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ht_cap_mcs_set_conversion() {
        let from = banjo_80211::Ieee80211HtCapabilities {
            ht_capability_info: 1,
            ampdu_params: 2,
            supported_mcs_set: banjo_80211::Ieee80211HtCapabilitiesSupportedMcsSet {
                bytes: [3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12],
            },
            ht_ext_capabilities: 0x13,
            tx_beamforming_capabilities: 0x14,
            asel_capabilities: 0x15,
        };
        let mcs_set = HtCapabilities::from(from).mcs_set;
        assert_eq!(
            mcs_set.as_bytes(),
            [3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12]
        );
    }

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

    #[test]
    fn vht_mcs_nss_map_accessor() {
        let mut map = VhtMcsNssMap(0x00ff);
        assert_eq!(map.ss(1), Ok(VhtMcsSet(3)));
        assert_eq!(map.ss(5), Ok(VhtMcsSet(0)));
        assert_eq!(map.set_ss(1, VhtMcsSet(2)), Ok(()));
        assert_eq!(map.set_ss(8, VhtMcsSet(3)), Ok(()));
        assert_eq!(map.ss(1), Ok(VhtMcsSet(2)));
        assert_eq!(map.ss(8), Ok(VhtMcsSet(3)));
        assert_eq!(map.0, 0xc0fe);
    }

    #[test]
    fn vht_mcs_nss_map_accssor_error() {
        let mut map = VhtMcsNssMap(0);
        assert_eq!(
            map.ss(0),
            Err("spatial stream number must be between 1 and 8, 0 invalid".to_string())
        );
        assert_eq!(
            map.ss(9),
            Err("spatial stream number must be between 1 and 8, 9 invalid".to_string())
        );
        assert_eq!(
            map.set_ss(0, VhtMcsSet(3)),
            Err("spatial stream number must be between 1 and 8, 0 invalid".to_string())
        );
        assert_eq!(
            map.set_ss(9, VhtMcsSet(3)),
            Err("spatial stream number must be between 1 and 8, 9 invalid".to_string())
        );
        assert_eq!(
            map.set_ss(1, VhtMcsSet(4)),
            Err("bitfield is only 2 bit wide, 4 invalid".to_string())
        );
    }

    unsafe fn as_bytes<T>(s: &T) -> &[u8] {
        std::slice::from_raw_parts((s as *const T) as *const u8, ::std::mem::size_of::<T>())
    }

    #[test]
    fn ddk_conversion_ht_capabilities() {
        let ht_cap = crate::ie::fake_ies::fake_ht_capabilities();
        let ddk: banjo_80211::Ieee80211HtCapabilities = ht_cap.into();
        assert_eq!(ht_cap.as_bytes(), unsafe { as_bytes(&ddk) });

        let ht_cap: HtCapabilities = ddk.into();
        assert_eq!(unsafe { as_bytes(&ddk) }, ht_cap.as_bytes());
    }

    #[test]
    fn ddk_conversion_vht_capabilities() {
        let vht_cap = crate::ie::fake_ies::fake_vht_capabilities();
        let ddk: banjo_80211::Ieee80211VhtCapabilities = vht_cap.into();
        assert_eq!(vht_cap.as_bytes(), unsafe { as_bytes(&ddk) });

        let vht_cap: VhtCapabilities = ddk.into();
        assert_eq!(unsafe { as_bytes(&ddk) }, vht_cap.as_bytes());
    }

    #[test]
    fn ddk_conversion_ht_operation() {
        let ht_op = crate::ie::fake_ies::fake_ht_operation();
        let ddk: banjo_wlan_info::WlanHtOp = ht_op.into();
        assert_eq!(ht_op.as_bytes(), unsafe { as_bytes(&ddk) });
    }

    #[test]
    fn ddk_conversion_vht_operation() {
        let vht_op = crate::ie::fake_ies::fake_vht_operation();
        let ddk: banjo_wlan_info::WlanVhtOp = vht_op.into();
        assert_eq!(vht_op.as_bytes(), unsafe { as_bytes(&ddk) });
    }
}
