// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie,
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    std::{collections::HashMap, hash::Hash},
};

#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum Protection {
    Open = 0,
    Wep = 1,
    Wpa1 = 2,
    Rsna = 3,
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
        let protection = self.get_protection();
        protection == Protection::Rsna || protection == Protection::Wpa1
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
        match self.rsn.as_ref() {
            Some(_) => Protection::Rsna,
            None if !self.cap.privacy => Protection::Open,
            None if self.find_wpa_ie().is_some() => Protection::Wpa1,
            None => Protection::Wep,
        }
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
            fake_frames::{fake_wpa1_ie, fake_wpa1_ie_body, fake_wpa2_rsne},
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
        Wpa2,
    }

    #[test]
    fn test_get_protection() {
        assert_eq!(Protection::Open, bss(ProtectionCfg::Open).get_protection());
        assert_eq!(Protection::Wep, bss(ProtectionCfg::Wep).get_protection());
        assert_eq!(Protection::Wpa1, bss(ProtectionCfg::Wpa1).get_protection());
        assert_eq!(Protection::Rsna, bss(ProtectionCfg::Wpa2).get_protection());
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
                ProtectionCfg::Wpa2 => Some(fake_wpa2_rsne()),
                _ => None,
            },
            vendor_ies: match protection {
                ProtectionCfg::Wpa1 => Some(fake_wpa1_ie()),
                _ => None,
            },
            ..bss
        }
    }
}
