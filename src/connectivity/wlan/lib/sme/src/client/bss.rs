// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::rsn::{is_wpa2_rsn_compatible, is_wpa3_rsn_compatible},
    crate::{Config, Ssid},
    fidl_fuchsia_wlan_mlme::BssDescription,
    std::{cmp::Ordering, collections::HashSet},
    wlan_common::{
        bss::{BssDescriptionExt, Protection},
        ie::{self, rsn::rsne, wsc},
    },
};

#[derive(Default, Debug, Copy, Clone, PartialEq, Eq)]
pub struct ClientConfig {
    cfg: Config,
    pub wpa3_supported: bool,
}

impl ClientConfig {
    pub fn from_config(cfg: Config, wpa3_supported: bool) -> Self {
        Self { cfg, wpa3_supported }
    }

    /// Converts a given BssDescription into a BssInfo.
    pub fn convert_bss_description(
        &self,
        bss: &BssDescription,
        wmm_param: Option<ie::WmmParam>,
    ) -> BssInfo {
        let mut probe_resp_wsc = None;
        match bss.find_wsc_ie() {
            Some(ie) => match wsc::parse_probe_resp_wsc(ie) {
                Ok(wsc) => probe_resp_wsc = Some(wsc),
                Err(_e) => {
                    // Parsing could fail because the WSC IE comes from a beacon, which does
                    // not contain all the information that a probe response WSC is expected
                    // to have. We don't have the information to distinguish between a beacon
                    // and a probe response, so we let this case fail silently.
                }
            },
            None => (),
        }

        BssInfo {
            bssid: bss.bssid.clone(),
            ssid: bss.ssid.clone(),
            rx_dbm: get_rx_dbm(bss),
            snr_db: bss.snr_db,
            channel: bss.chan.primary,
            protection: bss.get_protection(),
            compatible: self.is_bss_compatible(bss),
            probe_resp_wsc,
            wmm_param,
        }
    }

    /// Compares two BSS based on
    /// (1) their compatibility
    /// (2) their security protocol
    /// (3) their Beacon's RSSI
    pub fn compare_bss(&self, left: &BssDescription, right: &BssDescription) -> Ordering {
        self.is_bss_compatible(left)
            .cmp(&self.is_bss_compatible(right))
            .then(left.get_protection().cmp(&right.get_protection()))
            .then(compare_dbm(get_rx_dbm(left), get_rx_dbm(right)))
    }

    /// Determines whether a given BSS is compatible with this client SME configuration.
    pub fn is_bss_compatible(&self, bss: &BssDescription) -> bool {
        let privacy = wlan_common::mac::CapabilityInfo(bss.cap).privacy();
        let protection = bss.get_protection();
        match &protection {
            Protection::Open => true,
            Protection::Wep => self.cfg.wep_supported,
            Protection::Wpa1 => self.cfg.wpa1_supported,
            Protection::Wpa2Wpa3Personal | Protection::Wpa3Personal if self.wpa3_supported => {
                match bss.rsne.as_ref() {
                    Some(rsne) if privacy => match rsne::from_bytes(&rsne[..]) {
                        Ok((_, a_rsne)) => is_wpa3_rsn_compatible(&a_rsne),
                        _ => false,
                    },
                    _ => false,
                }
            }
            Protection::Wpa1Wpa2Personal
            | Protection::Wpa2Personal
            | Protection::Wpa2Wpa3Personal => match bss.rsne.as_ref() {
                Some(rsne) if privacy => match rsne::from_bytes(&rsne[..]) {
                    Ok((_, a_rsne)) => is_wpa2_rsn_compatible(&a_rsne),
                    _ => false,
                },
                _ => false,
            },
            _ => false,
        }
    }

    /// Returns the 'best' BSS from a given BSS list. The 'best' BSS is determined by comparing
    /// all BSS with `compare_bss(BssDescription, BssDescription)`.
    pub fn get_best_bss<'a>(&self, bss_list: &'a [BssDescription]) -> Option<&'a BssDescription> {
        bss_list.iter().max_by(|x, y| self.compare_bss(x, y))
    }

    /// Counts unique SSID in a given BSS list
    pub fn count_ssid(&self, bss_set: &[BssDescription]) -> usize {
        bss_set.into_iter().map(|b| b.ssid.clone()).collect::<HashSet<_>>().len()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct BssInfo {
    pub bssid: [u8; 6],
    pub ssid: Ssid,
    pub rx_dbm: i8,
    pub snr_db: i8,
    pub channel: u8,
    pub protection: Protection,
    pub compatible: bool,
    pub probe_resp_wsc: Option<wsc::ProbeRespWsc>,
    pub wmm_param: Option<ie::WmmParam>,
}

fn get_rx_dbm(bss: &BssDescription) -> i8 {
    if bss.rcpi_dbmh != 0 {
        (bss.rcpi_dbmh / 2) as i8
    } else {
        bss.rssi_dbm
    }
}

fn compare_dbm(left: i8, right: i8) -> Ordering {
    match (left, right) {
        (0, 0) => Ordering::Equal,
        (0, _) => Ordering::Less,
        (_, 0) => Ordering::Greater,
        (left, right) => left.cmp(&right),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::test_utils::{fake_bss_with_bssid, fake_wmm_param},
        fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
        std::cmp::Ordering,
        wlan_common::ie,
    };

    enum ProtectionCfg {
        Open,
        Wep,
        Wpa1,
        Wpa2Legacy,
        Wpa2,
        Wpa3,
        Wpa2NoPrivacy,
        Wpa2Wpa3MixedMode,
        Eap,
    }

    #[test]
    fn compare() {
        //  BSSes with the same RCPI, RSSI, and protection are equivalent.
        let cfg = ClientConfig::default();
        assert_eq!(
            Ordering::Equal,
            cfg.compare_bss(
                &bss(-10, -30, ProtectionCfg::Wpa2),
                &bss(-10, -30, ProtectionCfg::Wpa2)
            )
        );
        // Compatibility takes priority over everything else
        assert_bss_cmp(
            &cfg,
            &bss(-10, -10, ProtectionCfg::Wpa1),
            &bss(-50, -50, ProtectionCfg::Wpa2),
        );
        assert_bss_cmp(
            &cfg,
            &bss(-10, -10, ProtectionCfg::Wpa1),
            &bss(-50, -50, ProtectionCfg::Wpa2),
        );
        // Higher security is better.
        assert_bss_cmp(
            &cfg,
            &bss(-10, -10, ProtectionCfg::Open),
            &bss(-50, -50, ProtectionCfg::Wpa2),
        );

        // RCPI in dBmh takes priority over RSSI in dBmh
        assert_bss_cmp(
            &cfg,
            &bss(-20, -30, ProtectionCfg::Wpa2),
            &bss(-30, -20, ProtectionCfg::Wpa2),
        );
        // Compare RSSI if RCPI is absent
        assert_bss_cmp(&cfg, &bss(-30, 0, ProtectionCfg::Wpa2), &bss(-20, 0, ProtectionCfg::Wpa2));
        // Having an RCPI measurement is always better than not having any measurement
        assert_bss_cmp(&cfg, &bss(0, 0, ProtectionCfg::Wpa2), &bss(0, -200, ProtectionCfg::Wpa2));
        // Having an RSSI measurement is always better than not having any measurement
        assert_bss_cmp(&cfg, &bss(0, 0, ProtectionCfg::Wpa2), &bss(-100, 0, ProtectionCfg::Wpa2));
    }

    #[test]
    fn compare_with_wep_supported() {
        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        // WEP is supported while WPA1 is not, so we prefer it.
        assert_bss_cmp(
            &cfg,
            &bss(-10, -10, ProtectionCfg::Wpa1),
            &bss(-50, -50, ProtectionCfg::Wep),
        );
        assert_bss_cmp(
            &cfg,
            &bss(-10, -10, ProtectionCfg::Wep),
            &bss(-50, -50, ProtectionCfg::Wpa2),
        );
    }

    #[test]
    fn compare_with_wep_and_wpa1_supported() {
        let cfg = ClientConfig::from_config(Config::default().with_wep().with_wpa1(), false);
        // WEP is worse than WPA1 when both are supported.
        assert_bss_cmp(
            &cfg,
            &bss(-50, -50, ProtectionCfg::Wep),
            &bss(-10, -10, ProtectionCfg::Wpa1),
        );
    }

    #[test]
    fn get_best_bss_empty_list() {
        let cfg = ClientConfig::default();
        assert!(cfg.get_best_bss(&vec![]).is_none());
    }

    #[test]
    fn get_best_bss_nonempty_list() {
        let cfg = ClientConfig::default();
        let bss1 = bss(-30, -10, ProtectionCfg::Wep);
        let bss2 = bss(-20, -10, ProtectionCfg::Wpa2);
        let bss3 = bss(-80, -80, ProtectionCfg::Wpa2);
        let bss_list = vec![bss1, bss2, bss3];
        assert_eq!(cfg.get_best_bss(&bss_list), Some(&bss_list[1]));
    }

    #[test]
    fn verify_compatibility() {
        // Compatible:
        let cfg = ClientConfig::default();
        assert!(cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Open)));
        assert!(cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa2)));
        assert!(cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa2Wpa3MixedMode)));

        // Not compatible:
        assert!(!cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa1)));
        assert!(!cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa2Legacy)));
        assert!(!cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa2NoPrivacy)));
        assert!(!cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa3)));
        assert!(!cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Eap)));

        // WEP support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        assert!(cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wep)));

        // WPA3 support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default(), true);
        assert!(cfg.is_bss_compatible(&bss(-30, -10, ProtectionCfg::Wpa3)));
    }

    #[test]
    fn convert_bss() {
        let cfg = ClientConfig::default();
        assert_eq!(
            cfg.convert_bss_description(&bss(-30, -10, ProtectionCfg::Wpa2), None),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -5,
                snr_db: 0,
                channel: 1,
                protection: Protection::Wpa2Personal,
                compatible: true,
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );

        let wmm_param = *ie::parse_wmm_param(&fake_wmm_param().bytes[..])
            .expect("expect WMM param to be parseable");
        assert_eq!(
            cfg.convert_bss_description(&bss(-30, -10, ProtectionCfg::Wpa2), Some(wmm_param)),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -5,
                snr_db: 0,
                channel: 1,
                protection: Protection::Wpa2Personal,
                compatible: true,
                probe_resp_wsc: None,
                wmm_param: Some(wmm_param),
            }
        );

        assert_eq!(
            cfg.convert_bss_description(&bss(-30, -10, ProtectionCfg::Wep), None),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -5,
                snr_db: 0,
                channel: 1,
                protection: Protection::Wep,
                compatible: false,
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );

        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        assert_eq!(
            cfg.convert_bss_description(&bss(-30, -10, ProtectionCfg::Wep), None),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -5,
                snr_db: 0,
                channel: 1,
                protection: Protection::Wep,
                compatible: true,
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );
    }

    #[test]
    fn count_ssid_in_bss_list() {
        let cfg = ClientConfig::default();
        let bss1 = fake_bss_with_bssid(b"foo".to_vec(), [1, 1, 1, 1, 1, 1]);
        let bss2 = fake_bss_with_bssid(b"bar".to_vec(), [2, 2, 2, 2, 2, 2]);
        let bss3 = fake_bss_with_bssid(b"foo".to_vec(), [3, 3, 3, 3, 3, 3]);
        let num_ssid = cfg.count_ssid(&vec![bss1, bss2, bss3]);

        assert_eq!(2, num_ssid);
    }

    #[test]
    fn test_compare_dbm() {
        assert_eq!(compare_dbm(0, -5), Ordering::Less);
        assert_eq!(compare_dbm(-3, 0), Ordering::Greater);
        assert_eq!(compare_dbm(0, 0), Ordering::Equal);
        assert_eq!(compare_dbm(-6, -5), Ordering::Less);
        assert_eq!(compare_dbm(-3, -4), Ordering::Greater);
        assert_eq!(compare_dbm(-128, -128), Ordering::Equal);
    }

    // ======== helper functions below ======== //

    fn assert_bss_cmp(
        cfg: &ClientConfig,
        worse: &fidl_mlme::BssDescription,
        better: &fidl_mlme::BssDescription,
    ) {
        assert_eq!(Ordering::Less, cfg.compare_bss(worse, better));
        assert_eq!(Ordering::Greater, cfg.compare_bss(better, worse));
    }

    fn bss(_rssi_dbm: i8, _rcpi_dbmh: i16, protection: ProtectionCfg) -> fidl_mlme::BssDescription {
        let ret = fidl_mlme::BssDescription {
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: vec![],

            bss_type: fidl_mlme::BssTypes::Infrastructure,
            beacon_period: 100,
            dtim_period: 100,
            timestamp: 0,
            local_time: 0,

            cap: wlan_common::mac::CapabilityInfo(0)
                .with_privacy(match protection {
                    ProtectionCfg::Open | ProtectionCfg::Wpa2NoPrivacy => false,
                    _ => true,
                })
                .0,
            rates: vec![],
            country: None,
            rsne: match protection {
                ProtectionCfg::Wpa2Legacy => Some(fake_wpa2_legacy_rsne()),
                ProtectionCfg::Wpa2 | ProtectionCfg::Wpa2NoPrivacy => Some(fake_wpa2_rsne()),
                ProtectionCfg::Wpa3 => Some(fake_wpa3_rsne()),
                ProtectionCfg::Wpa2Wpa3MixedMode => Some(fake_wpa2_wpa3_mixed_mode_rsne()),
                ProtectionCfg::Eap => Some(fake_eap_rsne()),
                _ => None,
            },
            vendor_ies: match protection {
                ProtectionCfg::Wpa1 => Some(fake_wpa1_ie()),
                _ => None,
            },

            rcpi_dbmh: _rcpi_dbmh,
            rsni_dbh: 0,

            ht_cap: None,
            ht_op: None,
            vht_cap: None,
            vht_op: None,

            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
            rssi_dbm: _rssi_dbm,
            snr_db: 0,
        };
        ret
    }

    fn fake_wpa1_ie_body() -> Vec<u8> {
        vec![
            0x01, 0x00, // WPA version
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: AKM
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
        ]
    }

    fn fake_wpa1_ie() -> Vec<u8> {
        let mut ie = vec![
            0xdd, 0x16, 0x00, 0x50, 0xf2, // IE header
            0x01, // MSFT specific IE type (WPA)
        ];
        ie.append(&mut fake_wpa1_ie_body());
        ie
    }

    fn fake_wpa2_legacy_rsne() -> Vec<u8> {
        vec![
            48, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 2, // Group Cipher: TKIP
            1, 0, 0x00, 0x0F, 0xAC, 2, // 1 Pairwise Cipher: TKIP
            1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
        ]
    }

    fn fake_wpa2_rsne() -> Vec<u8> {
        vec![
            48, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
        ]
    }

    fn fake_wpa3_rsne() -> Vec<u8> {
        vec![
            48, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 8, // 1 AKM: SAE
            0xCC, 0x00, // RSN capabilities: MFP capable/required, 16 PTKSA replay counters
        ]
    }

    fn fake_wpa2_wpa3_mixed_mode_rsne() -> Vec<u8> {
        vec![
            48, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
            2, 0, 0x00, 0x0F, 0xAC, 8, 0x00, 0x0F, 0xAC, 2, // 2 AKM:  SAE, PSK
            0x8C, 0x00, // RSN capabilities: MFP capable, 16 PTKSA replay counters
        ]
    }

    fn fake_eap_rsne() -> Vec<u8> {
        vec![
            48, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 1, // 1 AKM:  802.1X
        ]
    }
}
