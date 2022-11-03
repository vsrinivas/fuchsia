// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
use {
    crate::ie, anyhow::format_err, banjo_fuchsia_wlan_common as banjo_common,
    fidl_fuchsia_wlan_common as fidl_common, std::fmt,
};

// IEEE Std 802.11-2016, Annex E
// Note the distinction of index for primary20 and index for center frequency.
// Fuchsia OS minimizes the use of the notion of center frequency,
// with following exceptions:
// - Cbw80P80's secondary frequency segment
// - Frequency conversion at device drivers
pub type MHz = u16;
pub const BASE_FREQ_2GHZ: MHz = 2407;
pub const BASE_FREQ_5GHZ: MHz = 5000;

pub const INVALID_CHAN_IDX: u8 = 0;

/// Channel bandwidth. Cbw80P80 requires the specification of
/// channel index corresponding to the center frequency
/// of the secondary consecutive frequency segment.
#[derive(Clone, Copy, Debug, Ord, PartialOrd, Eq, PartialEq)]
pub enum Cbw {
    Cbw20,
    Cbw40, // Same as Cbw40Above
    Cbw40Below,
    Cbw80,
    Cbw160,
    Cbw80P80 { secondary80: u8 },
}

impl Cbw {
    // TODO(fxbug.dev/83769): Implement `From `instead.
    pub fn to_fidl(&self) -> (fidl_common::ChannelBandwidth, u8) {
        match self {
            Cbw::Cbw20 => (fidl_common::ChannelBandwidth::Cbw20, 0),
            Cbw::Cbw40 => (fidl_common::ChannelBandwidth::Cbw40, 0),
            Cbw::Cbw40Below => (fidl_common::ChannelBandwidth::Cbw40Below, 0),
            Cbw::Cbw80 => (fidl_common::ChannelBandwidth::Cbw80, 0),
            Cbw::Cbw160 => (fidl_common::ChannelBandwidth::Cbw160, 0),
            Cbw::Cbw80P80 { secondary80 } => {
                (fidl_common::ChannelBandwidth::Cbw80P80, *secondary80)
            }
        }
    }

    pub fn to_banjo(&self) -> (banjo_common::ChannelBandwidth, u8) {
        match self {
            Cbw::Cbw20 => (banjo_common::ChannelBandwidth::CBW20, 0),
            Cbw::Cbw40 => (banjo_common::ChannelBandwidth::CBW40, 0),
            Cbw::Cbw40Below => (banjo_common::ChannelBandwidth::CBW40BELOW, 0),
            Cbw::Cbw80 => (banjo_common::ChannelBandwidth::CBW80, 0),
            Cbw::Cbw160 => (banjo_common::ChannelBandwidth::CBW160, 0),
            Cbw::Cbw80P80 { secondary80 } => {
                (banjo_common::ChannelBandwidth::CBW80P80, *secondary80)
            }
        }
    }

    pub fn from_fidl(fidl_cbw: fidl_common::ChannelBandwidth, fidl_secondary80: u8) -> Self {
        match fidl_cbw {
            fidl_common::ChannelBandwidth::Cbw20 => Cbw::Cbw20,
            fidl_common::ChannelBandwidth::Cbw40 => Cbw::Cbw40,
            fidl_common::ChannelBandwidth::Cbw40Below => Cbw::Cbw40Below,
            fidl_common::ChannelBandwidth::Cbw80 => Cbw::Cbw80,
            fidl_common::ChannelBandwidth::Cbw160 => Cbw::Cbw160,
            fidl_common::ChannelBandwidth::Cbw80P80 => {
                Cbw::Cbw80P80 { secondary80: fidl_secondary80 }
            }
        }
    }
}

/// A Channel defines the frequency spectrum to be used for radio synchronization.
/// See for sister definitions in FIDL and C/C++
///  - //sdk/fidl/fuchsia.wlan.common/wlan_common.fidl |struct wlan_channel_t|
///  - //sdk/fidl/fuchsia.wlan.mlme/wlan_mlme.fidl |struct WlanChan|
#[derive(Clone, Copy, Debug, Ord, PartialOrd, Eq, PartialEq)]
pub struct Channel {
    // TODO(porce): Augment with country and band
    pub primary: u8,
    pub cbw: Cbw,
}

// Fuchsia's short CBW notation. Not IEEE standard.
impl fmt::Display for Cbw {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Cbw::Cbw20 => write!(f, ""),       // Vanilla plain 20 MHz bandwidth
            Cbw::Cbw40 => write!(f, "+"),      // SCA, often denoted by "+1"
            Cbw::Cbw40Below => write!(f, "-"), // SCB, often denoted by "-1",
            Cbw::Cbw80 => write!(f, "V"),      // VHT 80 MHz (V from VHT)
            Cbw::Cbw160 => write!(f, "W"),     // VHT 160 MHz (as Wide as V + V ;) )
            Cbw::Cbw80P80 { secondary80 } => write!(f, "+{}P", secondary80), // VHT 80Plus80 (not often obvious, but P is the first alphabet)
        }
    }
}

impl fmt::Display for Channel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}{}", self.primary, self.cbw)
    }
}

impl Channel {
    pub fn new(primary: u8, cbw: Cbw) -> Self {
        Channel { primary, cbw }
    }

    // Weak validity test w.r.t the 2 GHz band primary channel only
    fn is_primary_2ghz(&self) -> bool {
        let p = self.primary;
        p >= 1 && p <= 14
    }

    // Weak validity test w.r.t the 5 GHz band primary channel only
    fn is_primary_5ghz(&self) -> bool {
        let p = self.primary;
        match p {
            36..=64 => (p - 36) % 4 == 0,
            100..=144 => (p - 100) % 4 == 0,
            149..=165 => (p - 149) % 4 == 0,
            _ => false,
        }
    }

    fn get_band_start_freq(&self) -> Result<MHz, anyhow::Error> {
        if self.is_primary_2ghz() {
            Ok(BASE_FREQ_2GHZ)
        } else if self.is_primary_5ghz() {
            Ok(BASE_FREQ_5GHZ)
        } else {
            return Err(format_err!("cannot get band start freq for channel {}", self));
        }
    }

    // Note get_center_chan_idx() is to assist channel validity test.
    // Return of Ok() does not imply the channel under test is valid.
    fn get_center_chan_idx(&self) -> Result<u8, anyhow::Error> {
        if !(self.is_primary_2ghz() || self.is_primary_5ghz()) {
            return Err(format_err!(
                "cannot get center channel index for an invalid primary channel {}",
                self
            ));
        }

        let p = self.primary;
        match self.cbw {
            Cbw::Cbw20 => Ok(p),
            Cbw::Cbw40 => Ok(p + 2),
            Cbw::Cbw40Below => Ok(p - 2),
            Cbw::Cbw80 | Cbw::Cbw80P80 { .. } => match p {
                36..=48 => Ok(42),
                52..=64 => Ok(58),
                100..=112 => Ok(106),
                116..=128 => Ok(122),
                132..=144 => Ok(138),
                148..=161_ => Ok(155),
                _ => {
                    return Err(format_err!(
                        "cannot get center channel index for invalid channel {}",
                        self
                    ))
                }
            },
            Cbw::Cbw160 => {
                // See IEEE Std 802.11-2016 Table 9-252 and 9-253.
                // Note CBW160 has only one frequency segment, regardless of
                // encodings on CCFS0 and CCFS1 in VHT Operation Information IE.
                match p {
                    36..=64 => Ok(50),
                    100..=128 => Ok(114),
                    _ => {
                        return Err(format_err!(
                            "cannot get center channel index for invalid channel {}",
                            self
                        ))
                    }
                }
            }
        }
    }

    /// Returns the center frequency of the first consecutive frequency segment of the channel
    /// in MHz if the channel is valid, Err(String) otherwise.
    pub fn get_center_freq(&self) -> Result<MHz, anyhow::Error> {
        // IEEE Std 802.11-2016, 21.3.14
        let start_freq = self.get_band_start_freq()?;
        let center_chan_idx = self.get_center_chan_idx()?;
        let spacing: MHz = 5;
        Ok(start_freq + spacing * center_chan_idx as u16)
    }

    /// Returns true if the primary channel index, channel bandwidth, and the secondary consecutive
    /// frequency segment (Cbw80P80 only) are all consistent and meet regulatory requirements of
    /// the USA. TODO(fxbug.dev/29490): Other countries.
    pub fn is_valid_in_us(&self) -> bool {
        if self.is_primary_2ghz() {
            self.is_valid_2ghz_in_us()
        } else if self.is_primary_5ghz() {
            self.is_valid_5ghz_in_us()
        } else {
            false
        }
    }

    fn is_valid_2ghz_in_us(&self) -> bool {
        if !self.is_primary_2ghz() {
            return false;
        }
        let p = self.primary;
        match self.cbw {
            Cbw::Cbw20 => p <= 11,
            Cbw::Cbw40 => p <= 7,
            Cbw::Cbw40Below => p >= 5,
            _ => false,
        }
    }

    fn is_valid_5ghz_in_us(&self) -> bool {
        if !self.is_primary_5ghz() {
            return false;
        }
        let p = self.primary;
        match self.cbw {
            Cbw::Cbw20 => true,
            Cbw::Cbw40 => p != 165 && (p % 8) == (if p <= 144 { 4 } else { 5 }),
            Cbw::Cbw40Below => p != 165 && (p % 8) == (if p <= 144 { 0 } else { 1 }),
            Cbw::Cbw80 => p != 165,
            Cbw::Cbw160 => p < 132,
            Cbw::Cbw80P80 { secondary80 } => {
                if p == 165 {
                    return false;
                }
                let valid_secondary80: [u8; 6] = [42, 58, 106, 122, 138, 155];
                if !valid_secondary80.contains(&secondary80) {
                    return false;
                }
                let ccfs0 = match self.get_center_chan_idx() {
                    Ok(v) => v,
                    Err(_) => return false,
                };
                let ccfs1 = secondary80;
                let gap = (ccfs0 as i16 - ccfs1 as i16).abs();
                gap > 16
            }
        }
    }

    /// Returns true if the channel is 2GHz. Does not perform validity checks.
    pub fn is_2ghz(&self) -> bool {
        self.is_primary_2ghz()
    }

    /// Returns true if the channel is 5GHz. Does not perform validity checks.
    pub fn is_5ghz(&self) -> bool {
        self.is_primary_5ghz()
    }

    fn is_unii1(&self) -> bool {
        let p = self.primary;
        p >= 32 && p <= 50
    }

    fn is_unii2a(&self) -> bool {
        let p = self.primary;
        // Note the overlap with U-NII-1
        p >= 50 && p <= 68
    }

    fn is_unii2c(&self) -> bool {
        let p = self.primary;
        p >= 96 && p <= 144
    }

    fn is_unii3(&self) -> bool {
        let p = self.primary;
        // Note the overlap with U-NII-2C
        p >= 138 && p <= 165
    }

    pub fn is_dfs(&self) -> bool {
        self.is_unii2a() || self.is_unii2c()
    }
}

impl From<Channel> for fidl_common::WlanChannel {
    fn from(channel: Channel) -> fidl_common::WlanChannel {
        fidl_common::WlanChannel::from(&channel)
    }
}

impl From<&Channel> for fidl_common::WlanChannel {
    fn from(channel: &Channel) -> fidl_common::WlanChannel {
        let (cbw, secondary80) = channel.cbw.to_fidl();
        fidl_common::WlanChannel { primary: channel.primary, cbw, secondary80 }
    }
}

impl From<fidl_common::WlanChannel> for Channel {
    fn from(fidl_channel: fidl_common::WlanChannel) -> Channel {
        Channel::from(&fidl_channel)
    }
}

impl From<&fidl_common::WlanChannel> for Channel {
    fn from(fidl_channel: &fidl_common::WlanChannel) -> Channel {
        Channel {
            primary: fidl_channel.primary,
            cbw: Cbw::from_fidl(fidl_channel.cbw, fidl_channel.secondary80),
        }
    }
}

impl From<Channel> for banjo_common::WlanChannel {
    fn from(channel: Channel) -> banjo_common::WlanChannel {
        let (cbw, secondary80) = channel.cbw.to_banjo();
        banjo_common::WlanChannel { primary: channel.primary, cbw, secondary80 }
    }
}

impl From<&Channel> for banjo_common::WlanChannel {
    fn from(channel: &Channel) -> banjo_common::WlanChannel {
        let (cbw, secondary80) = channel.cbw.to_banjo();
        banjo_common::WlanChannel { primary: channel.primary, cbw, secondary80 }
    }
}

/// Derive channel given DSSS param set, HT operation, and VHT operation IEs from
/// beacon or probe response, and the primary channel from which such frame is
/// received on.
///
/// Primary channel is extracted from HT op, DSSS param set, or `rx_primary_channel`,
/// in descending priority.
pub fn derive_channel(
    rx_primary_channel: u8,
    dsss_channel: Option<u8>,
    ht_op: Option<ie::HtOperation>,
    vht_op: Option<ie::VhtOperation>,
) -> fidl_common::WlanChannel {
    let primary = ht_op
        .as_ref()
        .map(|ht_op| ht_op.primary_channel)
        .or(dsss_channel)
        .unwrap_or(rx_primary_channel);

    let ht_op_cbw = ht_op.map(|ht_op| { ht_op.ht_op_info_head }.sta_chan_width());
    let vht_cbw_and_segs =
        vht_op.map(|vht_op| (vht_op.vht_cbw, vht_op.center_freq_seg0, vht_op.center_freq_seg1));

    let (cbw, secondary80) = match ht_op_cbw {
        // Inspect vht/ht op parameters to determine the channel width.
        Some(ie::StaChanWidth::ANY) => {
            // Safe to unwrap `ht_op` because `ht_op_cbw` is only Some(_) if `ht_op` has a value.
            let sec_chan_offset = { ht_op.unwrap().ht_op_info_head }.secondary_chan_offset();
            derive_wide_channel_bandwidth(vht_cbw_and_segs, sec_chan_offset)
        }
        // Default to Cbw20 if HT CBW field is set to 0 or not present.
        _ => Cbw::Cbw20,
    }
    .to_fidl();

    fidl_common::WlanChannel { primary, cbw, secondary80 }
}

/// Derive a CBW for a primary channel or channel switch.
/// VHT parameter derivation is defined identically by:
///     IEEE Std 802.11-2016 9.4.2.159 Table 9-252 for channel switching
///     IEEE Std 802.11-2016 11.40.1 Table 11-24 for VHT operation
/// SecChanOffset is defined identially by:
///     IEEE Std 802.11-2016 9.4.2.20 for channel switching
///     IEEE Std 802.11-2016 9.4.2.57 Table 9-168 for HT operation
pub fn derive_wide_channel_bandwidth(
    vht_cbw_and_segs: Option<(ie::VhtChannelBandwidth, u8, u8)>,
    sec_chan_offset: ie::SecChanOffset,
) -> Cbw {
    use ie::VhtChannelBandwidth as Vcb;
    match vht_cbw_and_segs {
        Some((Vcb::CBW_80_160_80P80, _, 0)) => Cbw::Cbw80,
        Some((Vcb::CBW_80_160_80P80, seg0, seg1)) if abs_sub(seg0, seg1) == 8 => Cbw::Cbw160,
        Some((Vcb::CBW_80_160_80P80, seg0, seg1)) if abs_sub(seg0, seg1) > 16 => {
            // See IEEE 802.11-2016, Table 9-252, about channel center frequency segment 1
            Cbw::Cbw80P80 { secondary80: seg1 }
        }
        // Use HT CBW if
        // - VHT op is not present,
        // - VHT op has deprecated parameters sets, or
        // - VHT CBW field is set to 0
        _ => match sec_chan_offset {
            ie::SecChanOffset::SECONDARY_ABOVE => Cbw::Cbw40,
            ie::SecChanOffset::SECONDARY_BELOW => Cbw::Cbw40Below,
            ie::SecChanOffset::SECONDARY_NONE | _ => Cbw::Cbw20,
        },
    }
}

fn abs_sub(v1: u8, v2: u8) -> u8 {
    if v2 >= v1 {
        v2 - v1
    } else {
        v1 - v2
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fmt_display() {
        let mut c = Channel::new(100, Cbw::Cbw40);
        assert_eq!(format!("{}", c), "100+");
        c.cbw = Cbw::Cbw160;
        assert_eq!(format!("{}", c), "100W");
        c.cbw = Cbw::Cbw80P80 { secondary80: 200 };
        assert_eq!(format!("{}", c), "100+200P");
    }

    #[test]
    fn test_is_primary_2ghz_or_5ghz() {
        // Note Cbw is ignored in this test.
        assert!(Channel::new(1, Cbw::Cbw160).is_primary_2ghz());
        assert!(!Channel::new(1, Cbw::Cbw160).is_primary_5ghz());

        assert!(Channel::new(12, Cbw::Cbw160).is_primary_2ghz());
        assert!(!Channel::new(12, Cbw::Cbw160).is_primary_5ghz());

        assert!(!Channel::new(36, Cbw::Cbw160).is_primary_2ghz());
        assert!(Channel::new(36, Cbw::Cbw160).is_primary_5ghz());

        assert!(!Channel::new(37, Cbw::Cbw160).is_primary_2ghz());
        assert!(!Channel::new(37, Cbw::Cbw160).is_primary_5ghz());

        assert!(!Channel::new(165, Cbw::Cbw160).is_primary_2ghz());
        assert!(Channel::new(165, Cbw::Cbw160).is_primary_5ghz());

        assert!(!Channel::new(166, Cbw::Cbw160).is_primary_2ghz());
        assert!(!Channel::new(166, Cbw::Cbw160).is_primary_5ghz());
    }

    #[test]
    fn test_band_start_freq() {
        assert_eq!(BASE_FREQ_2GHZ, Channel::new(1, Cbw::Cbw20).get_band_start_freq().unwrap());
        assert_eq!(BASE_FREQ_5GHZ, Channel::new(100, Cbw::Cbw20).get_band_start_freq().unwrap());
        assert!(Channel::new(15, Cbw::Cbw20).get_band_start_freq().is_err());
        assert!(Channel::new(200, Cbw::Cbw20).get_band_start_freq().is_err());
    }

    #[test]
    fn test_get_center_chan_idx() {
        assert!(Channel::new(1, Cbw::Cbw80).get_center_chan_idx().is_err());
        assert_eq!(9, Channel::new(11, Cbw::Cbw40Below).get_center_chan_idx().unwrap());
        assert_eq!(8, Channel::new(6, Cbw::Cbw40).get_center_chan_idx().unwrap());
        assert_eq!(36, Channel::new(36, Cbw::Cbw20).get_center_chan_idx().unwrap());
        assert_eq!(38, Channel::new(36, Cbw::Cbw40).get_center_chan_idx().unwrap());
        assert_eq!(42, Channel::new(36, Cbw::Cbw80).get_center_chan_idx().unwrap());
        assert_eq!(50, Channel::new(36, Cbw::Cbw160).get_center_chan_idx().unwrap());
        assert_eq!(
            42,
            Channel::new(36, Cbw::Cbw80P80 { secondary80: 155 }).get_center_chan_idx().unwrap()
        );
    }

    #[test]
    fn test_get_center_freq() {
        assert_eq!(2412 as MHz, Channel::new(1, Cbw::Cbw20).get_center_freq().unwrap());
        assert_eq!(2437 as MHz, Channel::new(6, Cbw::Cbw20).get_center_freq().unwrap());
        assert_eq!(2447 as MHz, Channel::new(6, Cbw::Cbw40).get_center_freq().unwrap());
        assert_eq!(2427 as MHz, Channel::new(6, Cbw::Cbw40Below).get_center_freq().unwrap());
        assert_eq!(5180 as MHz, Channel::new(36, Cbw::Cbw20).get_center_freq().unwrap());
        assert_eq!(5190 as MHz, Channel::new(36, Cbw::Cbw40).get_center_freq().unwrap());
        assert_eq!(5210 as MHz, Channel::new(36, Cbw::Cbw80).get_center_freq().unwrap());
        assert_eq!(5250 as MHz, Channel::new(36, Cbw::Cbw160).get_center_freq().unwrap());
        assert_eq!(
            5210 as MHz,
            Channel::new(36, Cbw::Cbw80P80 { secondary80: 155 }).get_center_freq().unwrap()
        );
    }

    #[test]
    fn test_valid_us_combo() {
        assert!(Channel::new(1, Cbw::Cbw20).is_valid_in_us());
        assert!(Channel::new(1, Cbw::Cbw40).is_valid_in_us());
        assert!(Channel::new(5, Cbw::Cbw40Below).is_valid_in_us());
        assert!(Channel::new(6, Cbw::Cbw20).is_valid_in_us());
        assert!(Channel::new(6, Cbw::Cbw40).is_valid_in_us());
        assert!(Channel::new(6, Cbw::Cbw40Below).is_valid_in_us());
        assert!(Channel::new(7, Cbw::Cbw40).is_valid_in_us());
        assert!(Channel::new(11, Cbw::Cbw20).is_valid_in_us());
        assert!(Channel::new(11, Cbw::Cbw40Below).is_valid_in_us());

        assert!(Channel::new(36, Cbw::Cbw20).is_valid_in_us());
        assert!(Channel::new(36, Cbw::Cbw40).is_valid_in_us());
        assert!(Channel::new(36, Cbw::Cbw160).is_valid_in_us());
        assert!(Channel::new(40, Cbw::Cbw20).is_valid_in_us());
        assert!(Channel::new(40, Cbw::Cbw40Below).is_valid_in_us());
        assert!(Channel::new(40, Cbw::Cbw160).is_valid_in_us());
        assert!(Channel::new(36, Cbw::Cbw80P80 { secondary80: 155 }).is_valid_in_us());
        assert!(Channel::new(40, Cbw::Cbw80P80 { secondary80: 155 }).is_valid_in_us());
        assert!(Channel::new(161, Cbw::Cbw80P80 { secondary80: 42 }).is_valid_in_us());
    }

    #[test]
    fn test_invalid_us_combo() {
        assert!(!Channel::new(1, Cbw::Cbw40Below).is_valid_in_us());
        assert!(!Channel::new(4, Cbw::Cbw40Below).is_valid_in_us());
        assert!(!Channel::new(8, Cbw::Cbw40).is_valid_in_us());
        assert!(!Channel::new(11, Cbw::Cbw40).is_valid_in_us());
        assert!(!Channel::new(6, Cbw::Cbw80).is_valid_in_us());
        assert!(!Channel::new(6, Cbw::Cbw160).is_valid_in_us());
        assert!(!Channel::new(6, Cbw::Cbw80P80 { secondary80: 155 }).is_valid_in_us());

        assert!(!Channel::new(36, Cbw::Cbw40Below).is_valid_in_us());
        assert!(!Channel::new(36, Cbw::Cbw80P80 { secondary80: 58 }).is_valid_in_us());
        assert!(!Channel::new(40, Cbw::Cbw40).is_valid_in_us());
        assert!(!Channel::new(40, Cbw::Cbw80P80 { secondary80: 42 }).is_valid_in_us());

        assert!(!Channel::new(165, Cbw::Cbw80).is_valid_in_us());
        assert!(!Channel::new(165, Cbw::Cbw80P80 { secondary80: 42 }).is_valid_in_us());
    }

    #[test]
    fn test_is_2ghz_or_5ghz() {
        assert!(Channel::new(1, Cbw::Cbw20).is_2ghz());
        assert!(!Channel::new(1, Cbw::Cbw20).is_5ghz());
        assert!(Channel::new(13, Cbw::Cbw20).is_2ghz());
        assert!(!Channel::new(13, Cbw::Cbw20).is_5ghz());
        assert!(Channel::new(36, Cbw::Cbw20).is_5ghz());
        assert!(!Channel::new(36, Cbw::Cbw20).is_2ghz());
    }

    #[test]
    fn test_is_dfs() {
        assert!(!Channel::new(1, Cbw::Cbw20).is_dfs());
        assert!(!Channel::new(36, Cbw::Cbw20).is_dfs());
        assert!(Channel::new(50, Cbw::Cbw20).is_dfs());
        assert!(Channel::new(144, Cbw::Cbw20).is_dfs());
        assert!(!Channel::new(149, Cbw::Cbw20).is_dfs());
    }

    #[test]
    fn test_convert_fidl_channel() {
        let mut f = fidl_common::WlanChannel::from(Channel::new(1, Cbw::Cbw20));
        assert!(
            f.primary == 1 && f.cbw == fidl_common::ChannelBandwidth::Cbw20 && f.secondary80 == 0
        );

        f = Channel::new(36, Cbw::Cbw80P80 { secondary80: 155 }).into();
        assert!(
            f.primary == 36
                && f.cbw == fidl_common::ChannelBandwidth::Cbw80P80
                && f.secondary80 == 155
        );

        let mut c = Channel::from(fidl_common::WlanChannel {
            primary: 11,
            cbw: fidl_common::ChannelBandwidth::Cbw40Below,
            secondary80: 123,
        });
        assert!(c.primary == 11 && c.cbw == Cbw::Cbw40Below);
        c = fidl_common::WlanChannel {
            primary: 149,
            cbw: fidl_common::ChannelBandwidth::Cbw80P80,
            secondary80: 42,
        }
        .into();
        assert!(c.primary == 149 && c.cbw == Cbw::Cbw80P80 { secondary80: 42 });
    }

    const RX_PRIMARY_CHAN: u8 = 11;
    const HT_PRIMARY_CHAN: u8 = 48;

    #[test]
    fn test_derive_channel_basic() {
        let channel = derive_channel(RX_PRIMARY_CHAN, None, None, None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: RX_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_with_dsss_param() {
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), None, None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: 6,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0
            }
        );
    }

    #[test]
    fn test_derive_channel_with_ht_20mhz() {
        let expected_channel = fidl_common::WlanChannel {
            primary: HT_PRIMARY_CHAN,
            cbw: fidl_common::ChannelBandwidth::Cbw20,
            secondary80: 0,
        };

        let test_params = [
            (ie::StaChanWidth::TWENTY_MHZ, ie::SecChanOffset::SECONDARY_NONE),
            (ie::StaChanWidth::TWENTY_MHZ, ie::SecChanOffset::SECONDARY_ABOVE),
            (ie::StaChanWidth::TWENTY_MHZ, ie::SecChanOffset::SECONDARY_BELOW),
            (ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_NONE),
        ];

        for (ht_width, sec_chan_offset) in test_params.iter() {
            let ht_op = ht_op(HT_PRIMARY_CHAN, *ht_width, *sec_chan_offset);
            let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), None);
            assert_eq!(channel, expected_channel);
        }
    }

    #[test]
    fn test_derive_channel_with_ht_40mhz() {
        let ht_op =
            ht_op(HT_PRIMARY_CHAN, ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_ABOVE);
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: HT_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw40,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_with_ht_40mhz_below() {
        let ht_op =
            ht_op(HT_PRIMARY_CHAN, ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_BELOW);
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: HT_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw40Below,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_with_vht_80mhz() {
        let ht_op =
            ht_op(HT_PRIMARY_CHAN, ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_ABOVE);
        let vht_op = vht_op(ie::VhtChannelBandwidth::CBW_80_160_80P80, 8, 0);
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), Some(vht_op));
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: HT_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw80,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_with_vht_160mhz() {
        let ht_op =
            ht_op(HT_PRIMARY_CHAN, ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_ABOVE);
        let vht_op = vht_op(ie::VhtChannelBandwidth::CBW_80_160_80P80, 0, 8);
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), Some(vht_op));
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: HT_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw160,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_with_vht_80plus80mhz() {
        let ht_op =
            ht_op(HT_PRIMARY_CHAN, ie::StaChanWidth::ANY, ie::SecChanOffset::SECONDARY_ABOVE);
        let vht_op = vht_op(ie::VhtChannelBandwidth::CBW_80_160_80P80, 18, 1);
        let channel = derive_channel(RX_PRIMARY_CHAN, Some(6), Some(ht_op), Some(vht_op));
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: HT_PRIMARY_CHAN,
                cbw: fidl_common::ChannelBandwidth::Cbw80P80,
                secondary80: 1,
            }
        );
    }

    #[test]
    fn test_derive_channel_none() {
        let channel = derive_channel(8, None, None, None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: 8,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0,
            }
        );
    }

    #[test]
    fn test_derive_channel_no_rx_primary() {
        let channel = derive_channel(8, Some(6), None, None);
        assert_eq!(
            channel,
            fidl_common::WlanChannel {
                primary: 6,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0,
            }
        )
    }

    fn ht_op(
        primary: u8,
        chan_width: ie::StaChanWidth,
        offset: ie::SecChanOffset,
    ) -> ie::HtOperation {
        let mut info_head = ie::HtOpInfoHead(0);
        info_head.set_sta_chan_width(chan_width);
        info_head.set_secondary_chan_offset(offset);
        ie::HtOperation {
            primary_channel: primary,
            ht_op_info_head: info_head,
            ht_op_info_tail: ie::HtOpInfoTail(0),
            basic_ht_mcs_set: ie::SupportedMcsSet(0),
        }
    }

    fn vht_op(vht_cbw: ie::VhtChannelBandwidth, seg0: u8, seg1: u8) -> ie::VhtOperation {
        ie::VhtOperation {
            vht_cbw,
            center_freq_seg0: seg0,
            center_freq_seg1: seg1,
            basic_mcs_nss: ie::VhtMcsNssMap(0),
        }
    }
}
