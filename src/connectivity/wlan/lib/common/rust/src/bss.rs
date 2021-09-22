// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        format::MacFmt as _,
        hasher::WlanHasher,
        ie::{
            self,
            rsn::suite_filter,
            wsc::{parse_probe_resp_wsc, ProbeRespWsc},
            IeType,
        },
        mac::CapabilityInfo,
    },
    anyhow::format_err,
    fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_sme as fidl_sme,
    ieee80211::{Bssid, Ssid},
    static_assertions::assert_eq_size,
    std::{
        cmp::Ordering,
        collections::HashMap,
        convert::{TryFrom, TryInto},
        fmt,
        hash::Hash,
        ops::Range,
    },
    zerocopy::{AsBytes, LayoutVerified},
};

// TODO(fxbug.dev/29885): Represent this as bitfield instead.
#[derive(Clone, Copy, Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum Protection {
    /// Higher number on Protection enum indicates a more preferred protection type for our SME.
    /// TODO(fxbug.dev/29877): Move all ordering logic to SME.
    Unknown = 0,
    Open = 1,
    Wep = 2,
    Wpa1 = 3,
    Wpa1Wpa2PersonalTkipOnly = 4,
    Wpa2PersonalTkipOnly = 5,
    Wpa1Wpa2Personal = 6,
    Wpa2Personal = 7,
    Wpa2Wpa3Personal = 8,
    Wpa3Personal = 9,
    Wpa2Enterprise = 10,
    /// WPA3 Enterprise 192-bit mode. WPA3 spec specifies an optional 192-bit mode but says nothing
    /// about a non 192-bit version. Thus, colloquially, it's likely that the term WPA3 Enterprise
    /// will be used to refer to WPA3 Enterprise 192-bit mode.
    Wpa3Enterprise = 11,
}

impl From<Protection> for fidl_sme::Protection {
    fn from(protection: Protection) -> fidl_sme::Protection {
        match protection {
            Protection::Unknown => fidl_sme::Protection::Unknown,
            Protection::Open => fidl_sme::Protection::Open,
            Protection::Wep => fidl_sme::Protection::Wep,
            Protection::Wpa1 => fidl_sme::Protection::Wpa1,
            Protection::Wpa1Wpa2PersonalTkipOnly => fidl_sme::Protection::Wpa1Wpa2PersonalTkipOnly,
            Protection::Wpa2PersonalTkipOnly => fidl_sme::Protection::Wpa2PersonalTkipOnly,
            Protection::Wpa1Wpa2Personal => fidl_sme::Protection::Wpa1Wpa2Personal,
            Protection::Wpa2Personal => fidl_sme::Protection::Wpa2Personal,
            Protection::Wpa2Wpa3Personal => fidl_sme::Protection::Wpa2Wpa3Personal,
            Protection::Wpa3Personal => fidl_sme::Protection::Wpa3Personal,
            Protection::Wpa2Enterprise => fidl_sme::Protection::Wpa2Enterprise,
            Protection::Wpa3Enterprise => fidl_sme::Protection::Wpa3Enterprise,
        }
    }
}

impl fmt::Display for Protection {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Protection::Unknown => write!(f, "{}", "Unknown"),
            Protection::Open => write!(f, "{}", "Open"),
            Protection::Wep => write!(f, "{}", "WEP"),
            Protection::Wpa1 => write!(f, "{}", "WPA1"),
            Protection::Wpa1Wpa2PersonalTkipOnly => write!(f, "{}", "WPA1/2 PSK TKIP"),
            Protection::Wpa2PersonalTkipOnly => write!(f, "{}", "WPA2 PSK TKIP"),
            Protection::Wpa1Wpa2Personal => write!(f, "{}", "WPA1/2 PSK"),
            Protection::Wpa2Personal => write!(f, "{}", "WPA2 PSK"),
            Protection::Wpa2Wpa3Personal => write!(f, "{}", "WPA2/3 PSK"),
            Protection::Wpa3Personal => write!(f, "{}", "WPA3 PSK"),
            Protection::Wpa2Enterprise => write!(f, "{}", "WPA2 802.1X"),
            Protection::Wpa3Enterprise => write!(f, "{}", "WPA3 802.1X"),
        }
    }
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum Standard {
    Dot11A,
    Dot11B,
    Dot11G,
    Dot11N,
    Dot11Ac,
}

#[derive(Debug, Clone, PartialEq)]
pub struct BssDescription {
    // *** Fields originally in fidl_internal::BssDescription
    pub ssid: Ssid,
    pub bssid: Bssid,
    pub bss_type: fidl_internal::BssType,
    pub beacon_period: u16,
    pub timestamp: u64,
    pub local_time: u64,
    pub capability_info: u16,
    pub channel: fidl_fuchsia_wlan_common::WlanChannel,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    // Private because the parsed information reference the IEs
    ies: Vec<u8>,

    // *** Fields parsed out of fidl_internal::BssDescription IEs
    // IEEE Std 802.11-2016 9.4.2.3
    // in 0.5 Mbps, with MSB indicating basic rate. See Table 9-78 for 126, 127.
    // The rates here may include both the basic rates and extended rates, which are not
    // continuous slices, hence we cannot use `Range`.
    rates: Vec<ie::SupportedRate>,
    tim_range: Option<Range<usize>>,
    country_range: Option<Range<usize>>,
    rsne_range: Option<Range<usize>>,
    ht_cap_range: Option<Range<usize>>,
    ht_op_range: Option<Range<usize>>,
    rm_enabled_cap_range: Option<Range<usize>>,
    ext_cap_range: Option<Range<usize>>,
    vht_cap_range: Option<Range<usize>>,
    vht_op_range: Option<Range<usize>>,
}

impl BssDescription {
    pub fn rates(&self) -> &[ie::SupportedRate] {
        &self.rates[..]
    }

    pub fn dtim_period(&self) -> u8 {
        self.tim_range
            .as_ref()
            .map(|range|
            // Safe to unwrap because we made sure TIM is parseable in `from_fidl`
            ie::parse_tim(&self.ies[range.clone()]).unwrap().header.dtim_period)
            .unwrap_or(0)
    }

    pub fn country(&self) -> Option<&[u8]> {
        self.country_range.as_ref().map(|range| &self.ies[range.clone()])
    }

    pub fn rsne(&self) -> Option<&[u8]> {
        self.rsne_range.as_ref().map(|range| &self.ies[range.clone()])
    }

    pub fn ht_cap(&self) -> Option<LayoutVerified<&[u8], ie::HtCapabilities>> {
        self.ht_cap_range.clone().map(|range| {
            // Safe to unwrap because we already verified HT caps is parseable in `from_fidl`
            ie::parse_ht_capabilities(&self.ies[range]).unwrap()
        })
    }

    pub fn raw_ht_cap(&self) -> Option<fidl_internal::HtCapabilities> {
        type HtCapArray = [u8; fidl_internal::HT_CAP_LEN as usize];
        self.ht_cap().map(|ht_cap| {
            assert_eq_size!(ie::HtCapabilities, HtCapArray);
            let bytes: HtCapArray = ht_cap.as_bytes().try_into().unwrap();
            fidl_internal::HtCapabilities { bytes }
        })
    }

    pub fn ht_op(&self) -> Option<LayoutVerified<&[u8], ie::HtOperation>> {
        self.ht_op_range.clone().map(|range| {
            // Safe to unwrap because we already verified HT op is parseable in `from_fidl`
            ie::parse_ht_operation(&self.ies[range]).unwrap()
        })
    }

    pub fn rm_enabled_cap(&self) -> Option<LayoutVerified<&[u8], ie::RmEnabledCapabilities>> {
        self.rm_enabled_cap_range.clone().map(|range| {
            // Safe to unwrap because we already verified RM enabled cap is parseable in `from_fidl`
            ie::parse_rm_enabled_capabilities(&self.ies[range]).unwrap()
        })
    }

    pub fn ext_cap(&self) -> Option<ie::ExtCapabilitiesView<&[u8]>> {
        self.ext_cap_range.clone().map(|range| ie::parse_ext_capabilities(&self.ies[range]))
    }

    pub fn raw_ht_op(&self) -> Option<fidl_internal::HtOperation> {
        type HtOpArray = [u8; fidl_internal::HT_OP_LEN as usize];
        self.ht_op().map(|ht_op| {
            assert_eq_size!(ie::HtOperation, HtOpArray);
            let bytes: HtOpArray = ht_op.as_bytes().try_into().unwrap();
            fidl_internal::HtOperation { bytes }
        })
    }

    pub fn vht_cap(&self) -> Option<LayoutVerified<&[u8], ie::VhtCapabilities>> {
        self.vht_cap_range.clone().map(|range| {
            // Safe to unwrap because we already verified VHT caps is parseable in `from_fidl`
            ie::parse_vht_capabilities(&self.ies[range]).unwrap()
        })
    }

    pub fn raw_vht_cap(&self) -> Option<fidl_internal::VhtCapabilities> {
        type VhtCapArray = [u8; fidl_internal::VHT_CAP_LEN as usize];
        self.vht_cap().map(|vht_cap| {
            assert_eq_size!(ie::VhtCapabilities, VhtCapArray);
            let bytes: VhtCapArray = vht_cap.as_bytes().try_into().unwrap();
            fidl_internal::VhtCapabilities { bytes }
        })
    }

    pub fn vht_op(&self) -> Option<LayoutVerified<&[u8], ie::VhtOperation>> {
        self.vht_op_range.clone().map(|range| {
            // Safe to unwrap because we already verified VHT op is parseable in `from_fidl`
            ie::parse_vht_operation(&self.ies[range]).unwrap()
        })
    }

    pub fn raw_vht_op(&self) -> Option<fidl_internal::VhtOperation> {
        type VhtOpArray = [u8; fidl_internal::VHT_OP_LEN as usize];
        self.vht_op().map(|vht_op| {
            assert_eq_size!(ie::VhtOperation, VhtOpArray);
            let bytes: VhtOpArray = vht_op.as_bytes().try_into().unwrap();
            fidl_internal::VhtOperation { bytes }
        })
    }

    pub fn ies(&self) -> &[u8] {
        &self.ies[..]
    }

    /// Return bool on whether BSS is protected.
    pub fn is_protected(&self) -> bool {
        self.protection() != Protection::Open
    }

    /// Return bool on whether BSS has security type that would require exchanging EAPOL frames.
    pub fn needs_eapol_exchange(&self) -> bool {
        match self.protection() {
            Protection::Unknown | Protection::Open | Protection::Wep => false,
            _ => true,
        }
    }

    /// Categorize BSS on what protection it supports.
    pub fn protection(&self) -> Protection {
        if !CapabilityInfo(self.capability_info).privacy() {
            return Protection::Open;
        }

        let supports_wpa_1 = self
            .wpa_ie()
            .map(|wpa_ie| {
                let rsne = ie::rsn::rsne::Rsne {
                    group_data_cipher_suite: Some(wpa_ie.multicast_cipher),
                    pairwise_cipher_suites: wpa_ie.unicast_cipher_list,
                    akm_suites: wpa_ie.akm_list,
                    ..Default::default()
                };
                suite_filter::WPA1_PERSONAL.is_satisfied(&rsne)
            })
            .unwrap_or(false);

        let rsne = match self.rsne() {
            Some(rsne) => match ie::rsn::rsne::from_bytes(rsne) {
                Ok((_, rsne)) => rsne,
                Err(_e) => {
                    return Protection::Unknown;
                }
            },
            None if self.find_wpa_ie().is_some() => {
                if supports_wpa_1 {
                    return Protection::Wpa1;
                } else {
                    return Protection::Unknown;
                }
            }
            None => return Protection::Wep,
        };

        let rsn_caps = rsne.rsn_capabilities.as_ref().unwrap_or(&ie::rsn::rsne::RsnCapabilities(0));
        let mfp_req = rsn_caps.mgmt_frame_protection_req();
        let mfp_cap = rsn_caps.mgmt_frame_protection_cap();

        if suite_filter::WPA3_PERSONAL.is_satisfied(&rsne) {
            if suite_filter::WPA2_PERSONAL.is_satisfied(&rsne) {
                if mfp_cap {
                    return Protection::Wpa2Wpa3Personal;
                }
            } else if mfp_cap && mfp_req {
                return Protection::Wpa3Personal;
            }
        }
        if suite_filter::WPA2_PERSONAL.is_satisfied(&rsne) {
            if supports_wpa_1 {
                return Protection::Wpa1Wpa2Personal;
            } else {
                return Protection::Wpa2Personal;
            }
        }
        if suite_filter::WPA2_PERSONAL_TKIP_ONLY.is_satisfied(&rsne) {
            if supports_wpa_1 {
                return Protection::Wpa1Wpa2PersonalTkipOnly;
            } else {
                return Protection::Wpa2PersonalTkipOnly;
            }
        }
        if supports_wpa_1 {
            return Protection::Wpa1;
        }
        if suite_filter::WPA3_ENTERPRISE_192_BIT.is_satisfied(&rsne) {
            if mfp_cap && mfp_req {
                return Protection::Wpa3Enterprise;
            }
        }
        if suite_filter::WPA2_ENTERPRISE.is_satisfied(&rsne) {
            return Protection::Wpa2Enterprise;
        }
        Protection::Unknown
    }

    /// Get the latest WLAN standard that the BSS supports.
    pub fn latest_standard(&self) -> Standard {
        if self.vht_cap().is_some() && self.vht_op().is_some() {
            Standard::Dot11Ac
        } else if self.ht_cap().is_some() && self.ht_op().is_some() {
            Standard::Dot11N
        } else if self.channel.primary <= 14 {
            if self.rates.iter().any(|r| match r.rate() {
                12 | 18 | 24 | 36 | 48 | 72 | 96 | 108 => true,
                _ => false,
            }) {
                Standard::Dot11G
            } else {
                Standard::Dot11B
            }
        } else {
            Standard::Dot11A
        }
    }

    /// Search for vendor-specific Info Element for WPA. If found, return the body.
    pub fn find_wpa_ie(&self) -> Option<&[u8]> {
        ie::Reader::new(&self.ies[..])
            .filter_map(|(id, ie)| match id {
                ie::Id::VENDOR_SPECIFIC => match ie::parse_vendor_ie(ie) {
                    Ok(ie::VendorIe::MsftLegacyWpa(body)) => Some(&body[..]),
                    _ => None,
                },
                _ => None,
            })
            .next()
    }

    /// Search for WPA Info Element and parse it. If no WPA Info Element is found, or a WPA Info
    /// Element is found but is not valid, return an error.
    pub fn wpa_ie(&self) -> Result<ie::wpa::WpaIe, anyhow::Error> {
        ie::parse_wpa_ie(self.find_wpa_ie().ok_or(format_err!("no wpa ie found"))?)
            .map_err(|e| e.into())
    }

    /// Search for vendor-specific Info Element for WMM Parameter. If found, return the body.
    pub fn find_wmm_param(&self) -> Option<&[u8]> {
        ie::Reader::new(&self.ies[..])
            .filter_map(|(id, ie)| match id {
                ie::Id::VENDOR_SPECIFIC => match ie::parse_vendor_ie(ie) {
                    Ok(ie::VendorIe::WmmParam(body)) => Some(&body[..]),
                    _ => None,
                },
                _ => None,
            })
            .next()
    }

    /// Search for WMM Parameter Element and parse it. If no WMM Parameter Element is found,
    /// return an error.
    pub fn wmm_param(&self) -> Result<LayoutVerified<&[u8], ie::WmmParam>, anyhow::Error> {
        ie::parse_wmm_param(self.find_wmm_param().ok_or(format_err!("no wmm parameter found"))?)
            .map_err(|e| e.into())
    }

    /// Search for the WiFi Simple Configuration Info Element. If found, return the body.
    pub fn find_wsc_ie(&self) -> Option<&[u8]> {
        ie::Reader::new(&self.ies[..])
            .filter_map(|(id, ie)| match id {
                ie::Id::VENDOR_SPECIFIC => match ie::parse_vendor_ie(ie) {
                    Ok(ie::VendorIe::Wsc(body)) => Some(&body[..]),
                    _ => None,
                },
                _ => None,
            })
            .next()
    }

    pub fn probe_resp_wsc(&self) -> Option<ProbeRespWsc> {
        match self.find_wsc_ie() {
            Some(ie) => match parse_probe_resp_wsc(ie) {
                Ok(wsc) => Some(wsc),
                // Parsing could fail because the WSC IE comes from a beacon, which does
                // not contain all the information that a probe response WSC is expected
                // to have. We don't have the information to distinguish between a beacon
                // and a probe response, so we let this case fail silently.
                Err(_) => None,
            },
            None => None,
        }
    }

    pub fn supports_uapsd(&self) -> bool {
        let wmm_info = ie::Reader::new(&self.ies[..])
            .filter_map(|(id, ie)| match id {
                ie::Id::VENDOR_SPECIFIC => match ie::parse_vendor_ie(ie) {
                    Ok(ie::VendorIe::WmmInfo(body)) => {
                        ie::parse_wmm_info(body).map(|wmm_info| *wmm_info).ok()
                    }
                    Ok(ie::VendorIe::WmmParam(body)) => {
                        ie::parse_wmm_param(body).map(|wmm_param| wmm_param.wmm_info).ok()
                    }
                    _ => None,
                },
                _ => None,
            })
            .next();
        wmm_info.map(|wmm_info| wmm_info.ap_wmm_info().uapsd()).unwrap_or(false)
    }

    /// IEEE 802.11-2016 4.5.4.8
    pub fn supports_ft(&self) -> bool {
        ie::Reader::new(&self.ies[..]).any(|(id, _ie)| id == ie::Id::MOBILITY_DOMAIN)
    }

    /// Returns a simplified BssCandidacy which implements PartialOrd.
    pub fn candidacy(&self) -> BssCandidacy {
        let rssi_dbm = self.rssi_dbm;
        match rssi_dbm {
            // The value 0 is considered a marker for an invalid RSSI and is therefore
            // transformed to the minimum RSSI value.
            0 => BssCandidacy { protection: self.protection(), rssi_dbm: i8::MIN },
            _ => BssCandidacy { protection: self.protection(), rssi_dbm },
        }
    }

    /// Returns an obfuscated string representation of the BssDescriptionExt suitable
    /// for protecting the privacy of an SSID and BSSID.
    // TODO(fxbug.dev/71906): Hashing SSID and BSSID should be removed once log redaction
    // retains consistent identifiers across Inspect and syslog.
    pub fn to_string(&self, hasher: &WlanHasher) -> String {
        format!(
            "SSID: {}, BSSID: {}, Protection: {}, Pri Chan: {}, Rx dBm: {}",
            hasher.hash_ssid(&self.ssid),
            hasher.hash_mac_addr(&self.bssid.0),
            self.protection(),
            self.channel.primary,
            self.rssi_dbm,
        )
    }

    /// Returns a string representation of the BssDescriptionExt. This representation
    /// is not suitable for protecting the privacy of an SSID and BSSID.
    pub fn to_non_obfuscated_string(&self) -> String {
        format!(
            "SSID: {}, BSSID: {}, Protection: {}, Pri Chan: {}, Rx dBm: {}",
            self.ssid.to_string_not_redactable(),
            self.bssid.0.to_mac_string(),
            self.protection(),
            self.channel.primary,
            self.rssi_dbm,
        )
    }
}

impl From<BssDescription> for fidl_internal::BssDescription {
    fn from(bss: BssDescription) -> fidl_internal::BssDescription {
        fidl_internal::BssDescription {
            bssid: bss.bssid.0,
            bss_type: bss.bss_type,
            beacon_period: bss.beacon_period,
            timestamp: bss.timestamp,
            local_time: bss.local_time,
            capability_info: bss.capability_info,
            channel: bss.channel,
            rssi_dbm: bss.rssi_dbm,
            snr_db: bss.snr_db,
            ies: bss.ies,
        }
    }
}

// TODO(fxbug.dev/83708): The error printed should include a minimal amount of information
// about the BSS Description that could not be converted to aid debugging.
impl TryFrom<fidl_internal::BssDescription> for BssDescription {
    type Error = anyhow::Error;

    fn try_from(bss: fidl_internal::BssDescription) -> Result<BssDescription, Self::Error> {
        let mut ssid_range = None;
        let mut rates = None;
        let mut tim_range = None;
        let mut country_range = None;
        let mut rsne_range = None;
        let mut ht_cap_range = None;
        let mut ht_op_range = None;
        let mut rm_enabled_cap_range = None;
        let mut ext_cap_range = None;
        let mut vht_cap_range = None;
        let mut vht_op_range = None;

        for (ie_type, range) in ie::IeSummaryIter::new(&bss.ies[..]) {
            let body = &bss.ies[range.clone()];
            match ie_type {
                IeType::SSID => {
                    ie::parse_ssid(body)?;
                    ssid_range = Some(range);
                }
                IeType::SUPPORTED_RATES => {
                    rates.get_or_insert(vec![]).extend(&*ie::parse_supported_rates(body)?);
                }
                IeType::EXTENDED_SUPPORTED_RATES => {
                    rates.get_or_insert(vec![]).extend(&*ie::parse_extended_supported_rates(body)?);
                }
                IeType::TIM => {
                    ie::parse_tim(body)?;
                    tim_range = Some(range);
                }
                IeType::COUNTRY => country_range = Some(range),
                // Decrement start of range by two to include the IE header.
                IeType::RSNE => rsne_range = Some(range.start - 2..range.end),
                IeType::HT_CAPABILITIES => {
                    ie::parse_ht_capabilities(body)?;
                    ht_cap_range = Some(range);
                }
                IeType::HT_OPERATION => {
                    ie::parse_ht_operation(body)?;
                    ht_op_range = Some(range);
                }
                IeType::RM_ENABLED_CAPABILITIES => {
                    if let Ok(_) = ie::parse_rm_enabled_capabilities(body) {
                        rm_enabled_cap_range = Some(range);
                    }
                }
                IeType::EXT_CAPABILITIES => {
                    // Parsing ExtCapabilities always succeeds, so no need to test parsing it here
                    ext_cap_range = Some(range);
                }
                IeType::VHT_CAPABILITIES => {
                    ie::parse_vht_capabilities(body)?;
                    vht_cap_range = Some(range);
                }
                IeType::VHT_OPERATION => {
                    ie::parse_vht_operation(body)?;
                    vht_op_range = Some(range);
                }
                _ => (),
            }
        }

        let ssid_range = ssid_range.ok_or_else(|| format_err!("Missing SSID IE"))?;
        let rates = rates.ok_or_else(|| format_err!("Missing rates IE"))?;

        Ok(Self {
            ssid: Ssid::from_bytes_unchecked(bss.ies[ssid_range].to_vec()),
            bssid: Bssid(bss.bssid),
            bss_type: bss.bss_type,
            beacon_period: bss.beacon_period,
            timestamp: bss.timestamp,
            local_time: bss.local_time,
            capability_info: bss.capability_info,
            channel: bss.channel,
            rssi_dbm: bss.rssi_dbm,
            snr_db: bss.snr_db,
            ies: bss.ies,

            rates,
            tim_range,
            country_range,
            rsne_range,
            ht_cap_range,
            ht_op_range,
            rm_enabled_cap_range,
            ext_cap_range,
            vht_cap_range,
            vht_op_range,
        })
    }
}

/// The BssCandidacy type is used to rank fidl_internal::BssDescription values. It is ordered
/// first by Protection and then by Dbm.
#[derive(Debug, Eq, PartialEq)]
pub struct BssCandidacy {
    protection: Protection,
    rssi_dbm: i8,
}

impl PartialOrd for BssCandidacy {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for BssCandidacy {
    fn cmp(&self, other: &Self) -> Ordering {
        self.protection.cmp(&other.protection).then(self.rssi_dbm.cmp(&other.rssi_dbm))
    }
}

/// Given a list of BssDescription, categorize each one based on the latest PHY standard it
/// supports and return a mapping from Standard to number of BSS.
pub fn phy_standard_map(bss_list: &Vec<BssDescription>) -> HashMap<Standard, usize> {
    info_map(bss_list, |bss| bss.latest_standard())
}

/// Given a list of BssDescription, return a mapping from channel to the number of BSS using
/// that channel.
pub fn channel_map(bss_list: &Vec<BssDescription>) -> HashMap<u8, usize> {
    info_map(bss_list, |bss| bss.channel.primary)
}

fn info_map<F, T>(bss_list: &Vec<BssDescription>, f: F) -> HashMap<T, usize>
where
    T: Eq + Hash,
    F: Fn(&BssDescription) -> T,
{
    let mut info_map: HashMap<T, usize> = HashMap::new();
    for bss in bss_list {
        *info_map.entry(f(&bss)).or_insert(0) += 1
    }
    info_map
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            assert_variant, fake_bss_description,
            ie::{fake_ies::fake_wmm_param, IeType},
            test_utils::{
                fake_frames::{
                    fake_unknown_rsne, fake_wmm_param_body, fake_wpa1_ie_body, fake_wpa2_mfpc_rsne,
                    fake_wpa2_mfpr_rsne, fake_wpa2_rsne, fake_wpa2_wpa3_mfpr_rsne,
                    fake_wpa2_wpa3_no_mfp_rsne, invalid_wpa3_enterprise_192_bit_rsne,
                    invalid_wpa3_rsne,
                },
                fake_stas::IesOverrides,
            },
        },
        fidl_fuchsia_wlan_common as fidl_common,
    };

    #[test]
    fn test_known_protection() {
        assert_eq!(Protection::Open, fake_bss_description!(Open).protection());
        assert_eq!(Protection::Wep, fake_bss_description!(Wep).protection());
        assert_eq!(Protection::Wpa1, fake_bss_description!(Wpa1).protection());
        assert_eq!(Protection::Wpa1, fake_bss_description!(Wpa1Enhanced).protection());
        assert_eq!(
            Protection::Wpa1Wpa2PersonalTkipOnly,
            fake_bss_description!(Wpa1Wpa2TkipOnly).protection()
        );
        assert_eq!(
            Protection::Wpa2PersonalTkipOnly,
            fake_bss_description!(Wpa2TkipOnly).protection()
        );
        assert_eq!(Protection::Wpa1Wpa2Personal, fake_bss_description!(Wpa1Wpa2).protection());
        assert_eq!(Protection::Wpa2Personal, fake_bss_description!(Wpa2TkipCcmp).protection());
        assert_eq!(Protection::Wpa2Personal, fake_bss_description!(Wpa2).protection());
        assert_eq!(Protection::Wpa2Wpa3Personal, fake_bss_description!(Wpa2Wpa3).protection());
        assert_eq!(Protection::Wpa3Personal, fake_bss_description!(Wpa3).protection());
        assert_eq!(Protection::Wpa2Enterprise, fake_bss_description!(Wpa2Enterprise).protection());
        assert_eq!(Protection::Wpa3Enterprise, fake_bss_description!(Wpa3Enterprise).protection());
    }

    #[test]
    fn test_pmf_configs_supported() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, fake_wpa2_mfpc_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Wpa2Personal, bss.protection());

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, fake_wpa2_mfpr_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Wpa2Personal, bss.protection());

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, fake_wpa2_wpa3_mfpr_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Wpa2Wpa3Personal, bss.protection());
    }

    #[test]
    fn test_downgrade() {
        // If Wpa3 doesn't use MFP, ignore it and use Wpa2 instead.
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, fake_wpa2_wpa3_no_mfp_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Wpa2Personal, bss.protection());

        // Downgrade to Wpa1 as well.
        let bss = fake_bss_description!(Wpa1,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, invalid_wpa3_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Wpa1, bss.protection());
    }

    #[test]
    fn test_unknown_protection() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, fake_unknown_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Unknown, bss.protection());

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, invalid_wpa3_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Unknown, bss.protection());

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::RSNE, invalid_wpa3_enterprise_192_bit_rsne()[2..].to_vec())
        );
        assert_eq!(Protection::Unknown, bss.protection());
    }

    #[test]
    fn test_needs_eapol_exchange() {
        assert!(fake_bss_description!(Wpa1).needs_eapol_exchange());
        assert!(fake_bss_description!(Wpa2).needs_eapol_exchange());

        assert!(!fake_bss_description!(Open).needs_eapol_exchange());
        assert!(!fake_bss_description!(Wep).needs_eapol_exchange());
    }

    #[test]
    fn test_rm_enabled_cap_ie() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::RM_ENABLED_CAPABILITIES)
        );
        assert!(bss.rm_enabled_cap().is_none());

        #[rustfmt::skip]
        let rm_enabled_capabilities = vec![
            0x03, // link measurement and neighbor report enabled
            0x00, 0x00, 0x00, 0x00,
        ];
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::RM_ENABLED_CAPABILITIES)
                .set(IeType::RM_ENABLED_CAPABILITIES, rm_enabled_capabilities.clone())
        );
        assert_variant!(bss.rm_enabled_cap(), Some(cap) => {
            assert_eq!(cap.as_bytes(), &rm_enabled_capabilities[..]);
        });
    }

    #[test]
    fn test_ext_cap_ie() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::EXT_CAPABILITIES)
        );
        assert!(bss.ext_cap().is_none());

        #[rustfmt::skip]
        let ext_capabilities = vec![
            0x04, 0x00,
            0x08, // BSS transition supported
            0x00, 0x00, 0x00, 0x00, 0x40
        ];
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::EXT_CAPABILITIES)
                .set(IeType::EXT_CAPABILITIES, ext_capabilities.clone())
        );
        let ext_cap = bss.ext_cap().expect("expect bss.ext_cap() to be Some");
        assert_eq!(ext_cap.ext_caps_octet_1.map(|o| o.0), Some(0x04));
        assert_eq!(ext_cap.ext_caps_octet_2.map(|o| o.0), Some(0x00));
        assert_eq!(ext_cap.ext_caps_octet_3.map(|o| o.0), Some(0x08));
        assert_eq!(ext_cap.remaining, &[0x00, 0x00, 0x00, 0x00, 0x40]);
    }

    #[test]
    fn test_wpa_ie() {
        let mut buf = vec![];
        fake_bss_description!(Wpa1)
            .wpa_ie()
            .expect("failed to find WPA1 IE")
            .write_into(&mut buf)
            .expect("failed to serialize WPA1 IE");
        assert_eq!(fake_wpa1_ie_body(false), buf);
        fake_bss_description!(Wpa2).wpa_ie().expect_err("found unexpected WPA1 IE");
    }

    #[test]
    fn test_wmm_param() {
        let bss = fake_bss_description!(Wpa2, qos: true, wmm_param: Some(fake_wmm_param()));
        let wmm_param = bss.wmm_param().expect("failed to find wmm param");
        assert_eq!(fake_wmm_param_body(), wmm_param.as_bytes());
    }

    #[test]
    fn test_latest_standard_ac() {
        let bss = fake_bss_description!(Open,
            ies_overrides: IesOverrides::new()
                .set(IeType::VHT_CAPABILITIES, vec![0; fidl_internal::VHT_CAP_LEN as usize])
                .set(IeType::VHT_OPERATION, vec![0; fidl_internal::VHT_OP_LEN as usize]),
        );
        assert_eq!(Standard::Dot11Ac, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_n() {
        let bss = fake_bss_description!(Open,
            ies_overrides: IesOverrides::new()
                .set(IeType::HT_CAPABILITIES, vec![0; fidl_internal::HT_CAP_LEN as usize])
                .set(IeType::HT_OPERATION, vec![0; fidl_internal::HT_OP_LEN as usize])
                .remove(IeType::VHT_CAPABILITIES)
                .remove(IeType::VHT_OPERATION),
        );
        assert_eq!(Standard::Dot11N, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_g() {
        let bss = fake_bss_description!(Open,
            channel: fidl_common::WlanChannel {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
            },
            rates: vec![12],
            ies_overrides: IesOverrides::new()
                .remove(IeType::HT_CAPABILITIES)
                .remove(IeType::HT_OPERATION)
                .remove(IeType::VHT_CAPABILITIES)
                .remove(IeType::VHT_OPERATION),
        );
        assert_eq!(Standard::Dot11G, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_b() {
        let bss = fake_bss_description!(Open,
            channel: fidl_common::WlanChannel {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
            },
            rates: vec![2],
            ies_overrides: IesOverrides::new()
                .remove(IeType::HT_CAPABILITIES)
                .remove(IeType::HT_OPERATION)
                .remove(IeType::VHT_CAPABILITIES)
                .remove(IeType::VHT_OPERATION),
        );
        assert_eq!(Standard::Dot11B, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_b_with_basic() {
        let bss = fake_bss_description!(Open,
            channel: fidl_common::WlanChannel {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
            },
            rates: vec![ie::SupportedRate(2).with_basic(true).0],
            ies_overrides: IesOverrides::new()
                .remove(IeType::HT_CAPABILITIES)
                .remove(IeType::HT_OPERATION)
                .remove(IeType::VHT_CAPABILITIES)
                .remove(IeType::VHT_OPERATION),
        );
        assert_eq!(Standard::Dot11B, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_a() {
        let bss = fake_bss_description!(Open,
            channel: fidl_common::WlanChannel {
                primary: 36,
                secondary80: 0,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
            },
            rates: vec![48],
            ies_overrides: IesOverrides::new()
                .remove(IeType::HT_CAPABILITIES)
                .remove(IeType::HT_OPERATION)
                .remove(IeType::VHT_CAPABILITIES)
                .remove(IeType::VHT_OPERATION),
        );
        assert_eq!(Standard::Dot11A, bss.latest_standard());
    }

    #[test]
    fn test_supports_uapsd() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_INFO)
                .remove(IeType::WMM_PARAM)
        );
        assert!(!bss.supports_uapsd());

        let mut wmm_info = vec![0x80]; // U-APSD enabled
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_INFO)
                .remove(IeType::WMM_PARAM)
                .set(IeType::WMM_INFO, wmm_info.clone())
        );
        assert!(bss.supports_uapsd());

        wmm_info = vec![0x00]; // U-APSD not enabled
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_INFO)
                .remove(IeType::WMM_PARAM)
                .set(IeType::WMM_INFO, wmm_info)
        );
        assert!(!bss.supports_uapsd());

        #[rustfmt::skip]
        let mut wmm_param = vec![
            0x80, // U-APSD enabled
            0x00, // reserved
            0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
            0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
            0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
            0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        ];
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_INFO)
                .remove(IeType::WMM_PARAM)
                .set(IeType::WMM_PARAM, wmm_param.clone())
        );
        assert!(bss.supports_uapsd());

        wmm_param[0] = 0x00; // U-APSD not enabled
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::WMM_INFO)
                .remove(IeType::WMM_PARAM)
                .set(IeType::WMM_PARAM, wmm_param)
        );
        assert!(!bss.supports_uapsd());
    }

    #[test]
    fn test_supports_ft() {
        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::MOBILITY_DOMAIN)
        );
        assert!(!bss.supports_ft());

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .remove(IeType::MOBILITY_DOMAIN)
                // We only check that the IE exists, so just set the content to bytes 0's.
                .set(IeType::MOBILITY_DOMAIN, vec![0x00; 3])
        );
        assert!(bss.supports_ft());
    }

    #[test]
    fn test_candidacy() {
        let bss_candidacy = fake_bss_description!(Wpa2, rssi_dbm: -10).candidacy();
        assert_eq!(
            bss_candidacy,
            BssCandidacy { protection: Protection::Wpa2Personal, rssi_dbm: -10 }
        );

        let bss_candidacy = fake_bss_description!(Open, rssi_dbm: -10).candidacy();
        assert_eq!(bss_candidacy, BssCandidacy { protection: Protection::Open, rssi_dbm: -10 });

        let bss_candidacy = fake_bss_description!(Wpa2, rssi_dbm: -20).candidacy();
        assert_eq!(
            bss_candidacy,
            BssCandidacy { protection: Protection::Wpa2Personal, rssi_dbm: -20 }
        );

        let bss_candidacy = fake_bss_description!(Wpa2, rssi_dbm: 0).candidacy();
        assert_eq!(
            bss_candidacy,
            BssCandidacy { protection: Protection::Wpa2Personal, rssi_dbm: i8::MIN }
        );
    }

    fn assert_bss_comparison(worse: &BssDescription, better: &BssDescription) {
        assert_eq!(Ordering::Less, worse.candidacy().cmp(&better.candidacy()));
        assert_eq!(Ordering::Greater, better.candidacy().cmp(&worse.candidacy()));
    }

    #[test]
    fn test_bss_comparison() {
        //  Two BSSDescription values with the same protection and RSSI are equivalent.
        assert_eq!(
            Ordering::Equal,
            fake_bss_description!(Wpa2, rssi_dbm: -10)
                .candidacy()
                .cmp(&fake_bss_description!(Wpa2, rssi_dbm: -10).candidacy())
        );

        // Higher security is better.
        assert_bss_comparison(
            &fake_bss_description!(Wpa1, rssi_dbm: -10),
            &fake_bss_description!(Wpa2, rssi_dbm: -50),
        );
        assert_bss_comparison(
            &fake_bss_description!(Open, rssi_dbm: -10),
            &fake_bss_description!(Wpa2, rssi_dbm: -50),
        );
        // Higher RSSI is better if security is equivalent.
        assert_bss_comparison(
            &fake_bss_description!(Wpa2, rssi_dbm: -50),
            &fake_bss_description!(Wpa2, rssi_dbm: -10),
        );
        // Having an RSSI measurement is always better than not having any measurement
        assert_bss_comparison(
            &fake_bss_description!(Wpa2, rssi_dbm: 0),
            &fake_bss_description!(Wpa2, rssi_dbm: -100),
        );
    }

    #[test]
    fn test_bss_ie_fields() {
        #[rustfmt::skip]
        let ht_cap = vec![
            0xef, 0x09, // HT Capabilities Info
            0x1b, // A-MPDU Parameters: 0x1b
            0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
            0x00, 0x00, // HT Extended Capabilities
            0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
            0x00
        ];
        #[rustfmt::skip]
        let ht_op = vec![
            0x9d, // Primary Channel: 157
            0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
            0x00, 0x00, 0x00, 0x00, // HT Info Subsets
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        ];
        #[rustfmt::skip]
        let vht_cap = vec![
            0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
            0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        ];
        let vht_op = vec![0x01, 0x9b, 0x00, 0xfc, 0xff];

        let bss = fake_bss_description!(Wpa2,
            ies_overrides: IesOverrides::new()
                .set(IeType::SSID, b"ssidie".to_vec())
                .set(IeType::SUPPORTED_RATES, vec![0x81, 0x82, 0x83])
                .set(IeType::EXTENDED_SUPPORTED_RATES, vec![4, 5, 6])
                .set(IeType::COUNTRY, vec![1, 2, 3])
                .set(IeType::HT_CAPABILITIES, ht_cap.clone())
                .set(IeType::HT_OPERATION, ht_op.clone())
                .set(IeType::VHT_CAPABILITIES, vht_cap.clone())
                .set(IeType::VHT_OPERATION, vht_op.clone())
        );
        assert_eq!(bss.ssid, Ssid::try_from("ssidie").unwrap());
        assert_eq!(
            bss.rates(),
            &[
                ie::SupportedRate(0x81),
                ie::SupportedRate(0x82),
                ie::SupportedRate(0x83),
                ie::SupportedRate(4),
                ie::SupportedRate(5),
                ie::SupportedRate(6)
            ]
        );
        assert_eq!(bss.country(), Some(&[1, 2, 3][..]));
        assert_eq!(bss.rsne(), Some(&fake_wpa2_rsne()[..]));
        assert_variant!(bss.ht_cap(), Some(capability_info) => {
            assert_eq!(capability_info.bytes(), &ht_cap[..]);
        });
        assert_eq!(
            bss.raw_ht_cap().map(|capability_info| capability_info.bytes.to_vec()),
            Some(ht_cap)
        );
        assert_variant!(bss.ht_op(), Some(op) => {
            assert_eq!(op.bytes(), &ht_op[..]);
        });
        assert_eq!(bss.raw_ht_op().map(|op| op.bytes.to_vec()), Some(ht_op));
        assert_variant!(bss.vht_cap(), Some(capability_info) => {
            assert_eq!(capability_info.bytes(), &vht_cap[..]);
        });
        assert_eq!(
            bss.raw_vht_cap().map(|capability_info| capability_info.bytes.to_vec()),
            Some(vht_cap)
        );
        assert_variant!(bss.vht_op(), Some(op) => {
            assert_eq!(op.bytes(), &vht_op[..]);
        });
        assert_eq!(bss.raw_vht_op().map(|op| op.bytes.to_vec()), Some(vht_op));
    }
}
