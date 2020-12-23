// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        format::MacFmt as _,
        hasher::WlanHasher,
        ie::{self, rsn::suite_filter},
        mac::CapabilityInfo,
    },
    anyhow::format_err,
    fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_sme as fidl_sme,
    static_assertions::assert_eq_size,
    std::{cmp::Ordering, collections::HashMap, convert::TryInto, fmt, hash::Hash},
    zerocopy::AsBytes,
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
    Wpa2Legacy = 4,
    Wpa1Wpa2Personal = 5,
    Wpa2Personal = 6,
    Wpa2Wpa3Personal = 7,
    Wpa3Personal = 8,
    Wpa2Enterprise = 9,
    /// WPA3 Enterprise 192-bit mode. WPA3 spec specifies an optional 192-bit mode but says nothing
    /// about a non 192-bit version. Thus, colloquially, it's likely that the term WPA3 Enterprise
    /// will be used to refer to WPA3 Enterprise 192-bit mode.
    Wpa3Enterprise = 10,
}

impl From<Protection> for fidl_sme::Protection {
    fn from(protection: Protection) -> fidl_sme::Protection {
        match protection {
            Protection::Unknown => fidl_sme::Protection::Unknown,
            Protection::Open => fidl_sme::Protection::Open,
            Protection::Wep => fidl_sme::Protection::Wep,
            Protection::Wpa1 => fidl_sme::Protection::Wpa1,
            Protection::Wpa2Legacy => fidl_sme::Protection::Wpa2Legacy,
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
            Protection::Wep => write!(f, "{}", "Wep"),
            Protection::Wpa1 => write!(f, "{}", "Wpa1"),
            Protection::Wpa2Legacy => write!(f, "{}", "Wpa2Legacy"),
            Protection::Wpa1Wpa2Personal => write!(f, "{}", "Wpa1Wpa2Personal"),
            Protection::Wpa2Personal => write!(f, "{}", "Wpa2Personal"),
            Protection::Wpa2Wpa3Personal => write!(f, "{}", "Wpa2Wpa3Personal"),
            Protection::Wpa3Personal => write!(f, "{}", "Wpa3Personal"),
            Protection::Wpa2Enterprise => write!(f, "{}", "Wpa2Enterprise"),
            Protection::Wpa3Enterprise => write!(f, "{}", "Wpa3Enterprise"),
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
    pub bssid: [u8; 6],
    pub bss_type: fidl_internal::BssTypes,
    pub beacon_period: u16,
    pub timestamp: u64,
    pub local_time: u64,
    pub cap: u16,
    pub chan: fidl_fuchsia_wlan_common::WlanChan,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    pub ies: Vec<u8>,

    // *** Fields parsed out of fidl_internal::BssDescription IEs
    ssid: Vec<u8>,
    // IEEE Std 802.11-2016 9.4.2.3
    // in 0.5 Mbps, with MSB indicating basic rate. See Table 9-78 for 126, 127.
    rates: Vec<u8>,
    dtim_period: u8,
    country: Option<Vec<u8>>,
    rsne: Option<Vec<u8>>,
    ht_cap: Option<fidl_internal::HtCapabilities>,
    ht_op: Option<fidl_internal::HtOperation>,
    vht_cap: Option<fidl_internal::VhtCapabilities>,
    vht_op: Option<fidl_internal::VhtOperation>,
}

impl BssDescription {
    pub fn ssid(&self) -> &[u8] {
        &self.ssid[..]
    }

    pub fn rates(&self) -> &[u8] {
        &self.rates[..]
    }

    pub fn dtim_period(&self) -> u8 {
        self.dtim_period
    }

    pub fn country(&self) -> Option<&[u8]> {
        self.country.as_deref()
    }

    pub fn rsne(&self) -> Option<&[u8]> {
        self.rsne.as_deref()
    }

    pub fn ht_cap(&self) -> Option<&fidl_internal::HtCapabilities> {
        self.ht_cap.as_ref()
    }

    pub fn ht_op(&self) -> Option<&fidl_internal::HtOperation> {
        self.ht_op.as_ref()
    }

    pub fn vht_cap(&self) -> Option<&fidl_internal::VhtCapabilities> {
        self.vht_cap.as_ref()
    }

    pub fn vht_op(&self) -> Option<&fidl_internal::VhtOperation> {
        self.vht_op.as_ref()
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

        let rsne = match self.rsne.as_ref() {
            Some(rsne) => match ie::rsn::rsne::from_bytes(rsne) {
                Ok((_, rsne)) => rsne,
                Err(_e) => {
                    return Protection::Unknown;
                }
            },
            None if !CapabilityInfo(self.cap).privacy() => return Protection::Open,
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
                if mfp_cap && !mfp_req {
                    return Protection::Wpa2Wpa3Personal;
                }
            } else if mfp_cap && mfp_req {
                return Protection::Wpa3Personal;
            }
        } else if suite_filter::WPA2_PERSONAL.is_satisfied(&rsne) {
            if supports_wpa_1 || suite_filter::WPA2_LEGACY.is_satisfied(&rsne) {
                return Protection::Wpa1Wpa2Personal;
            } else {
                return Protection::Wpa2Personal;
            }
        } else if supports_wpa_1 {
            return Protection::Wpa1;
        } else if suite_filter::WPA2_LEGACY.is_satisfied(&rsne) {
            return Protection::Wpa2Legacy;
        } else if suite_filter::WPA3_ENTERPRISE_192_BIT.is_satisfied(&rsne) {
            if mfp_cap && mfp_req {
                return Protection::Wpa3Enterprise;
            }
        } else if suite_filter::WPA2_ENTERPRISE.is_satisfied(&rsne) {
            return Protection::Wpa2Enterprise;
        }
        Protection::Unknown
    }

    /// Get the latest WLAN standard that the BSS supports.
    pub fn latest_standard(&self) -> Standard {
        if self.vht_cap.is_some() && self.vht_op.is_some() {
            Standard::Dot11Ac
        } else if self.ht_cap.is_some() && self.ht_op.is_some() {
            Standard::Dot11N
        } else if self.chan.primary <= 14 {
            if self.rates.iter().any(|r| match ie::SupportedRate(*r).rate() {
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
    pub fn to_string(&self, hasher: &WlanHasher) -> String {
        format!(
            "SSID: {}, BSSID: {}, Protection: {}, Pri Chan: {}, Rx dBm: {}",
            hasher.hash_ssid(&self.ssid),
            hasher.hash_mac_addr(&self.bssid),
            self.protection(),
            self.chan.primary,
            self.rssi_dbm,
        )
    }

    /// Returns a string representation of the BssDescriptionExt. This representation
    /// is not suitable for protecting the privacy of an SSID and BSSID.
    pub fn to_non_obfuscated_string(&self) -> String {
        format!(
            "SSID: {}, BSSID: {}, Protection: {}, Pri Chan: {}, Rx dBm: {}",
            String::from_utf8(self.ssid.clone()).unwrap_or_else(|_| hex::encode(self.ssid.clone())),
            self.bssid.to_mac_str(),
            self.protection(),
            self.chan.primary,
            self.rssi_dbm,
        )
    }

    pub fn from_fidl(bss: fidl_internal::BssDescription) -> Result<Self, anyhow::Error> {
        type HtCapArray = [u8; fidl_internal::HT_CAP_LEN as usize];
        type HtOpArray = [u8; fidl_internal::HT_OP_LEN as usize];
        type VhtCapArray = [u8; fidl_internal::VHT_CAP_LEN as usize];
        type VhtOpArray = [u8; fidl_internal::VHT_OP_LEN as usize];

        let mut ssid = None;
        let mut rates = None;
        let mut dtim_period = None;
        let mut country = None;
        let mut rsne = None;
        let mut ht_cap = None;
        let mut ht_op = None;
        let mut vht_cap = None;
        let mut vht_op = None;

        for (id, body) in ie::Reader::new(&bss.ies[..]) {
            match id {
                ie::Id::SSID => ssid = Some(ie::parse_ssid(body)?.to_vec()),
                ie::Id::SUPPORTED_RATES | ie::Id::EXT_SUPPORTED_RATES => {
                    rates.get_or_insert_with(|| vec![]).extend_from_slice(body);
                }
                ie::Id::TIM => dtim_period = Some(ie::parse_tim(body)?.header.dtim_period),
                ie::Id::COUNTRY => country = Some(body.to_vec()),
                ie::Id::RSNE => {
                    let mut rsne_bytes = vec![id.0, body.len() as u8];
                    rsne_bytes.extend_from_slice(body);
                    rsne = Some(rsne_bytes);
                }
                ie::Id::HT_CAPABILITIES => {
                    let parsed_ht_cap = ie::parse_ht_capabilities(body)?;
                    assert_eq_size!(ie::HtCapabilities, HtCapArray);
                    let bytes: HtCapArray = parsed_ht_cap.as_bytes().try_into().unwrap();
                    ht_cap = Some(fidl_internal::HtCapabilities { bytes })
                }
                ie::Id::HT_OPERATION => {
                    let parsed_ht_op = ie::parse_ht_operation(body)?;
                    assert_eq_size!(ie::HtOperation, HtOpArray);
                    let bytes: HtOpArray = parsed_ht_op.as_bytes().try_into().unwrap();
                    ht_op = Some(fidl_internal::HtOperation { bytes });
                }
                ie::Id::VHT_CAPABILITIES => {
                    let parsed_vht_cap = ie::parse_vht_capabilities(body)?;
                    assert_eq_size!(ie::VhtCapabilities, VhtCapArray);
                    let bytes: VhtCapArray = parsed_vht_cap.as_bytes().try_into().unwrap();
                    vht_cap = Some(fidl_internal::VhtCapabilities { bytes });
                }
                ie::Id::VHT_OPERATION => {
                    let parsed_ht_op = ie::parse_vht_operation(body)?;
                    assert_eq_size!(ie::VhtOperation, VhtOpArray);
                    let bytes: VhtOpArray = parsed_ht_op.as_bytes().try_into().unwrap();
                    vht_op = Some(fidl_internal::VhtOperation { bytes });
                }
                _ => (),
            }
        }

        let ssid = ssid.ok_or_else(|| format_err!("Missing SSID IE"))?;
        let rates = rates.ok_or_else(|| format_err!("Missing rates IE"))?;

        Ok(Self {
            bssid: bss.bssid,
            bss_type: bss.bss_type,
            beacon_period: bss.beacon_period,
            timestamp: bss.timestamp,
            local_time: bss.local_time,
            cap: bss.cap,
            chan: bss.chan,
            rssi_dbm: bss.rssi_dbm,
            snr_db: bss.snr_db,
            ies: bss.ies,

            ssid,
            rates,
            dtim_period: dtim_period.unwrap_or(0),
            country,
            rsne,
            ht_cap,
            ht_op,
            vht_cap,
            vht_op,
        })
    }

    pub fn to_fidl(self) -> fidl_internal::BssDescription {
        fidl_internal::BssDescription {
            bssid: self.bssid,
            bss_type: self.bss_type,
            beacon_period: self.beacon_period,
            timestamp: self.timestamp,
            local_time: self.local_time,
            cap: self.cap,
            chan: self.chan,
            rssi_dbm: self.rssi_dbm,
            snr_db: self.snr_db,
            ies: self.ies,
        }
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
    info_map(bss_list, |bss| bss.chan.primary)
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
            fake_bss,
            ie::IeType,
            test_utils::{
                fake_frames::{
                    fake_unknown_rsne, fake_wpa1_ie_body, invalid_wpa2_wpa3_rsne,
                    invalid_wpa3_enterprise_192_bit_rsne, invalid_wpa3_rsne,
                },
                fake_stas::IesOverrides,
            },
        },
        fidl_fuchsia_wlan_common as fidl_common,
    };

    #[test]
    fn test_known_protection() {
        assert_eq!(Protection::Open, fake_bss!(Open).protection());
        assert_eq!(Protection::Wep, fake_bss!(Wep).protection());
        assert_eq!(Protection::Wpa1, fake_bss!(Wpa1).protection());
        assert_eq!(Protection::Wpa1, fake_bss!(Wpa1Enhanced).protection());
        assert_eq!(Protection::Wpa2Legacy, fake_bss!(Wpa2Legacy).protection());
        assert_eq!(Protection::Wpa1Wpa2Personal, fake_bss!(Wpa1Wpa2).protection());
        assert_eq!(Protection::Wpa1Wpa2Personal, fake_bss!(Wpa2Mixed).protection());
        assert_eq!(Protection::Wpa2Personal, fake_bss!(Wpa2).protection());
        assert_eq!(Protection::Wpa2Wpa3Personal, fake_bss!(Wpa2Wpa3).protection());
        assert_eq!(Protection::Wpa3Personal, fake_bss!(Wpa3).protection());
        assert_eq!(Protection::Wpa2Enterprise, fake_bss!(Wpa2Enterprise).protection());
        assert_eq!(Protection::Wpa3Enterprise, fake_bss!(Wpa3Enterprise).protection());
    }

    #[test]
    fn test_unknown_protection() {
        let mut bss = fake_bss!(Wpa2);
        bss.rsne = Some(fake_unknown_rsne());
        assert_eq!(Protection::Unknown, bss.protection());

        bss.rsne = Some(invalid_wpa2_wpa3_rsne());
        assert_eq!(Protection::Unknown, bss.protection());

        bss.rsne = Some(invalid_wpa3_rsne());
        assert_eq!(Protection::Unknown, bss.protection());

        bss.rsne = Some(invalid_wpa3_enterprise_192_bit_rsne());
        assert_eq!(Protection::Unknown, bss.protection());
    }

    #[test]
    fn test_needs_eapol_exchange() {
        assert!(fake_bss!(Wpa1).needs_eapol_exchange());
        assert!(fake_bss!(Wpa2).needs_eapol_exchange());

        assert!(!fake_bss!(Open).needs_eapol_exchange());
        assert!(!fake_bss!(Wep).needs_eapol_exchange());
    }

    #[test]
    fn test_wpa_ie() {
        let mut buf = vec![];
        fake_bss!(Wpa1)
            .wpa_ie()
            .expect("failed to find WPA1 IE")
            .write_into(&mut buf)
            .expect("failed to serialize WPA1 IE");
        assert_eq!(fake_wpa1_ie_body(false), buf);
        fake_bss!(Wpa2).wpa_ie().expect_err("found unexpected WPA1 IE");
    }

    #[test]
    fn test_latest_standard_ac() {
        let bss = fake_bss!(Open,
            ies_overrides: IesOverrides::new()
                .set(IeType::VHT_CAPABILITIES, vec![0; fidl_internal::VHT_CAP_LEN as usize])
                .set(IeType::VHT_OPERATION, vec![0; fidl_internal::VHT_OP_LEN as usize]),
        );
        assert_eq!(Standard::Dot11Ac, bss.latest_standard());
    }

    #[test]
    fn test_latest_standard_n() {
        let bss = fake_bss!(Open,
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
        let bss = fake_bss!(Open,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
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
        let bss = fake_bss!(Open,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
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
        let bss = fake_bss!(Open,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
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
        let bss = fake_bss!(Open,
            chan: fidl_common::WlanChan {
                primary: 36,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
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
    fn test_candidacy() {
        let bss_candidacy = fake_bss!(Wpa2, rssi_dbm: -10).candidacy();
        assert_eq!(
            bss_candidacy,
            BssCandidacy { protection: Protection::Wpa2Personal, rssi_dbm: -10 }
        );

        let bss_candidacy = fake_bss!(Open, rssi_dbm: -10).candidacy();
        assert_eq!(bss_candidacy, BssCandidacy { protection: Protection::Open, rssi_dbm: -10 });

        let bss_candidacy = fake_bss!(Wpa2, rssi_dbm: -20).candidacy();
        assert_eq!(
            bss_candidacy,
            BssCandidacy { protection: Protection::Wpa2Personal, rssi_dbm: -20 }
        );

        let bss_candidacy = fake_bss!(Wpa2, rssi_dbm: 0).candidacy();
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
            fake_bss!(Wpa2, rssi_dbm: -10)
                .candidacy()
                .cmp(&fake_bss!(Wpa2, rssi_dbm: -10).candidacy())
        );

        // Higher security is better.
        assert_bss_comparison(&fake_bss!(Wpa1, rssi_dbm: -10), &fake_bss!(Wpa2, rssi_dbm: -50));
        assert_bss_comparison(&fake_bss!(Open, rssi_dbm: -10), &fake_bss!(Wpa2, rssi_dbm: -50));
        // Higher RSSI is better if security is equivalent.
        assert_bss_comparison(&fake_bss!(Wpa2, rssi_dbm: -50), &fake_bss!(Wpa2, rssi_dbm: -10));
        // Having an RSSI measurement is always better than not having any measurement
        assert_bss_comparison(&fake_bss!(Wpa2, rssi_dbm: 0), &fake_bss!(Wpa2, rssi_dbm: -100));
    }
}
