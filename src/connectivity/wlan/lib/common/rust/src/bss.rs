// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie::{self, rsn::suite_filter},
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    std::{collections::HashMap, hash::Hash},
};

// TODO(WLAN-1273): Represent this as bitfield instead.
#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum Protection {
    /// Higher number on Protection enum indicates a more preferred protection type for our SME.
    /// TODO(WLAN-1264): Move all ordering logic to SME.
    Unknown = 0,
    Open = 1,
    Wep = 2,
    Wpa1 = 3,
    Wpa1Wpa2Personal = 4,
    Wpa2Personal = 5,
    Wpa2Wpa3Personal = 6,
    Wpa3Personal = 7,
    Wpa2Enterprise = 8,
    /// WPA3 Enterprise 192-bit mode. WPA3 spec specifies an optional 192-bit mode but says nothing
    /// about a non 192-bit version. Thus, colloquially, it's likely that the term WPA3 Enterprise
    /// will be used to refer to WPA3 Enterprise 192-bit mode.
    Wpa3Enterprise = 9,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum Standard {
    Dot11A,
    Dot11B,
    Dot11G,
    Dot11N,
    Dot11Ac,
}

pub trait BssDescriptionExt {
    /// Return bool on whether BSS is protected.
    fn is_protected(&self) -> bool {
        self.get_protection() != Protection::Open
    }
    /// Return bool on whether BSS has security type that would require exchanging EAPOL frames.
    fn needs_eapol_exchange(&self) -> bool {
        match self.get_protection() {
            Protection::Unknown | Protection::Open | Protection::Wep => false,
            _ => true,
        }
    }
    /// Categorize BSS on what protection it supports.
    fn get_protection(&self) -> Protection;
    /// Get the latest WLAN standard that the BSS supports.
    fn get_latest_standard(&self) -> Standard;
    /// Search for vendor-specific Info Element for WPA. If found, return the body.
    fn find_wpa_ie(&self) -> Option<&[u8]>;
    /// Search for WPA Info Element and parse it. If no WPA Info Element is found, or a WPA Info
    /// Element is found but is not valid, return an error.
    fn get_wpa_ie(&self) -> Result<ie::wpa::WpaIe, failure::Error>;
}

impl BssDescriptionExt for fidl_mlme::BssDescription {
    fn get_protection(&self) -> Protection {
        let has_valid_wpa1_vendor_ie = self
            .get_wpa_ie()
            .map(|wpa_ie| {
                let mut rsne = ie::rsn::rsne::Rsne::new();
                rsne.group_data_cipher_suite = Some(wpa_ie.multicast_cipher);
                rsne.pairwise_cipher_suites = wpa_ie.unicast_cipher_list;
                rsne.akm_suites = wpa_ie.akm_list;
                suite_filter::WPA1_PERSONAL.is_satisfied(&rsne)
            })
            .unwrap_or(false);

        let rsne = match self.rsn.as_ref() {
            Some(rsne) => match ie::rsn::rsne::from_bytes(rsne) {
                Ok((_, rsne)) => rsne,
                Err(_e) => return Protection::Unknown,
            },
            None if !self.cap.privacy => return Protection::Open,
            None if self.find_wpa_ie().is_some() => {
                if has_valid_wpa1_vendor_ie {
                    return Protection::Wpa1;
                } else {
                    return Protection::Unknown;
                }
            }
            None => return Protection::Wep,
        };

        let supports_wpa_1 =
            has_valid_wpa1_vendor_ie || suite_filter::WPA1_PERSONAL.is_satisfied(&rsne);
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
            if supports_wpa_1 {
                return Protection::Wpa1Wpa2Personal;
            } else {
                return Protection::Wpa2Personal;
            }
        } else if supports_wpa_1 {
            return Protection::Wpa1;
        } else if suite_filter::WPA3_ENTERPRISE_192_BIT.is_satisfied(&rsne) {
            if mfp_cap && mfp_req {
                return Protection::Wpa3Enterprise;
            }
        } else if suite_filter::WPA2_ENTERPRISE.is_satisfied(&rsne) {
            return Protection::Wpa2Enterprise;
        }
        Protection::Unknown
    }

    fn get_latest_standard(&self) -> Standard {
        if self.vht_cap.is_some() && self.vht_op.is_some() {
            Standard::Dot11Ac
        } else if self.ht_cap.is_some() && self.ht_op.is_some() {
            Standard::Dot11N
        } else if self.chan.primary <= 14 {
            if self.basic_rate_set.iter().any(|r| match r {
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

    fn find_wpa_ie(&self) -> Option<&[u8]> {
        let ies = self.vendor_ies.as_ref()?;
        ie::Reader::new(&ies[..])
            .filter_map(|(id, ie)| match id {
                ie::Id::VENDOR_SPECIFIC => match ie::parse_vendor_ie(ie) {
                    Ok(ie::VendorIe::MsftLegacyWpa(body)) => Some(&body[..]),
                    _ => None,
                },
                _ => None,
            })
            .next()
    }

    fn get_wpa_ie(&self) -> Result<ie::wpa::WpaIe, failure::Error> {
        ie::parse_wpa_ie(self.find_wpa_ie().ok_or(format_err!("no wpa ie found"))?)
            .map_err(|e| e.into())
    }
}

/// Given a list of BssDescription, categorize each one based on the latest PHY standard it
/// supports and return a mapping from Standard to number of BSS.
pub fn get_phy_standard_map(bss_list: &Vec<fidl_mlme::BssDescription>) -> HashMap<Standard, usize> {
    get_info_map(bss_list, |bss| bss.get_latest_standard())
}

/// Given a list of BssDescription, return a mapping from channel to the number of BSS using
/// that channel.
pub fn get_channel_map(bss_list: &Vec<fidl_mlme::BssDescription>) -> HashMap<u8, usize> {
    get_info_map(bss_list, |bss| bss.chan.primary)
}

fn get_info_map<F, T>(bss_list: &Vec<fidl_mlme::BssDescription>, f: F) -> HashMap<T, usize>
where
    T: Eq + Hash,
    F: Fn(&fidl_mlme::BssDescription) -> T,
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
        crate::test_utils::{
            fake_frames::{
                fake_unknown_rsne, fake_wpa1_ie, fake_wpa1_ie_body, fake_wpa2_enterprise_rsne,
                fake_wpa2_rsne, fake_wpa2_wpa3_rsne, fake_wpa3_enterprise_192_bit_rsne,
                fake_wpa3_rsne, invalid_wpa2_wpa3_rsne, invalid_wpa3_enterprise_192_bit_rsne,
                invalid_wpa3_rsne,
            },
            fake_stas::{
                fake_ht_capabilities, fake_ht_operation, fake_unprotected_bss_description,
                fake_vht_capabilities, fake_vht_operation,
            },
        },
    };

    enum ProtectionCfg {
        Open,
        Wep,
        Wpa1,
        Wpa1Wpa2,
        Wpa2,
        Wpa2Wpa3,
        Wpa3,
        Wpa2Enterprise,
        Wpa3Enterprise,
    }

    #[test]
    fn test_get_known_protection() {
        assert_eq!(Protection::Open, bss(ProtectionCfg::Open).get_protection());
        assert_eq!(Protection::Wep, bss(ProtectionCfg::Wep).get_protection());
        assert_eq!(Protection::Wpa1, bss(ProtectionCfg::Wpa1).get_protection());
        assert_eq!(Protection::Wpa1Wpa2Personal, bss(ProtectionCfg::Wpa1Wpa2).get_protection());
        assert_eq!(Protection::Wpa2Personal, bss(ProtectionCfg::Wpa2).get_protection());
        assert_eq!(Protection::Wpa2Wpa3Personal, bss(ProtectionCfg::Wpa2Wpa3).get_protection());
        assert_eq!(Protection::Wpa3Personal, bss(ProtectionCfg::Wpa3).get_protection());
        assert_eq!(Protection::Wpa2Enterprise, bss(ProtectionCfg::Wpa2Enterprise).get_protection());
        assert_eq!(Protection::Wpa3Enterprise, bss(ProtectionCfg::Wpa3Enterprise).get_protection());
    }

    #[test]
    fn test_get_unknown_protection() {
        let mut bss = bss(ProtectionCfg::Wpa2);
        bss.rsn = Some(fake_unknown_rsne());
        assert_eq!(Protection::Unknown, bss.get_protection());

        bss.rsn = Some(invalid_wpa2_wpa3_rsne());
        assert_eq!(Protection::Unknown, bss.get_protection());

        bss.rsn = Some(invalid_wpa3_rsne());
        assert_eq!(Protection::Unknown, bss.get_protection());

        bss.rsn = Some(invalid_wpa3_enterprise_192_bit_rsne());
        assert_eq!(Protection::Unknown, bss.get_protection());
    }

    #[test]
    fn test_needs_eapol_exchange() {
        assert!(bss(ProtectionCfg::Wpa1).needs_eapol_exchange());
        assert!(bss(ProtectionCfg::Wpa2).needs_eapol_exchange());

        assert!(!bss(ProtectionCfg::Open).needs_eapol_exchange());
        assert!(!bss(ProtectionCfg::Wep).needs_eapol_exchange());
    }

    #[test]
    fn test_get_wpa_ie() {
        let mut buf = vec![];
        bss(ProtectionCfg::Wpa1)
            .get_wpa_ie()
            .expect("failed to find WPA1 IE")
            .write_into(&mut buf)
            .expect("failed to serialize WPA1 IE");
        assert_eq!(fake_wpa1_ie_body(), buf);
        bss(ProtectionCfg::Wpa2).get_wpa_ie().expect_err("found unexpected WPA1 IE");
    }

    #[test]
    fn test_get_latest_standard_ac() {
        let mut bss = bss(ProtectionCfg::Open);
        bss.vht_cap = Some(Box::new(fake_vht_capabilities()));
        bss.vht_op = Some(Box::new(fake_vht_operation()));
        assert_eq!(Standard::Dot11Ac, bss.get_latest_standard());
    }

    #[test]
    fn test_get_latest_standard_n() {
        let mut bss = bss(ProtectionCfg::Open);
        bss.ht_cap = Some(Box::new(fake_ht_capabilities()));
        bss.ht_op = Some(Box::new(fake_ht_operation()));
        assert_eq!(Standard::Dot11N, bss.get_latest_standard());
    }

    #[test]
    fn test_get_latest_standard_g() {
        let mut bss = bss(ProtectionCfg::Open);
        bss.chan.primary = 1;
        bss.basic_rate_set = vec![12];
        assert_eq!(Standard::Dot11G, bss.get_latest_standard());
    }

    #[test]
    fn test_get_latest_standard_b() {
        let mut bss = bss(ProtectionCfg::Open);
        bss.chan.primary = 1;
        bss.basic_rate_set = vec![2];
        assert_eq!(Standard::Dot11B, bss.get_latest_standard());
    }

    #[test]
    fn test_get_latest_standard_a() {
        let mut bss = bss(ProtectionCfg::Open);
        bss.chan.primary = 36;
        assert_eq!(Standard::Dot11A, bss.get_latest_standard());
    }

    fn bss(protection: ProtectionCfg) -> fidl_mlme::BssDescription {
        let bss = fake_unprotected_bss_description(b"foo".to_vec());
        fidl_mlme::BssDescription {
            cap: fidl_mlme::CapabilityInfo {
                privacy: match protection {
                    ProtectionCfg::Open => false,
                    _ => true,
                },
                ..bss.cap
            },
            rsn: match protection {
                ProtectionCfg::Wpa3Enterprise => Some(fake_wpa3_enterprise_192_bit_rsne()),
                ProtectionCfg::Wpa2Enterprise => Some(fake_wpa2_enterprise_rsne()),
                ProtectionCfg::Wpa3 => Some(fake_wpa3_rsne()),
                ProtectionCfg::Wpa2Wpa3 => Some(fake_wpa2_wpa3_rsne()),
                ProtectionCfg::Wpa2 | ProtectionCfg::Wpa1Wpa2 => Some(fake_wpa2_rsne()),
                _ => None,
            },
            vendor_ies: match protection {
                ProtectionCfg::Wpa1 | ProtectionCfg::Wpa1Wpa2 => Some(fake_wpa1_ie()),
                _ => None,
            },
            ..bss
        }
    }
}
