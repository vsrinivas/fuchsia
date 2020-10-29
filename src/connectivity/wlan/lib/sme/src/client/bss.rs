// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{Config, Ssid},
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription},
    std::{cmp::Ordering, collections::HashSet},
    wlan_common::{
        bss::{BssDescriptionExt, Protection},
        channel::Channel,
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
            channel: Channel::from_fidl(bss.chan),
            protection: bss.get_protection(),
            compatible: self.is_bss_compatible(bss),
            ht_cap: bss.ht_cap.as_ref().map(|cap| **cap),
            vht_cap: bss.vht_cap.as_ref().map(|cap| **cap),
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
                        Ok((_, a_rsne)) => a_rsne.is_wpa3_rsn_compatible(),
                        _ => false,
                    },
                    _ => false,
                }
            }
            Protection::Wpa1Wpa2Personal
            | Protection::Wpa2Personal
            | Protection::Wpa2Wpa3Personal => match bss.rsne.as_ref() {
                Some(rsne) if privacy => match rsne::from_bytes(&rsne[..]) {
                    Ok((_, a_rsne)) => a_rsne.is_wpa2_rsn_compatible(),
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
    pub channel: wlan_common::channel::Channel,
    pub protection: Protection,
    pub compatible: bool,
    pub ht_cap: Option<fidl_mlme::HtCapabilities>,
    pub vht_cap: Option<fidl_mlme::VhtCapabilities>,
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
        crate::client::test_utils::fake_wmm_param,
        fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
        std::cmp::Ordering,
        wlan_common::channel::Cbw,
        wlan_common::{
            fake_bss,
            ie::{
                self,
                fake_ies::{fake_ht_cap_bytes, fake_vht_cap_bytes},
            },
        },
    };

    #[test]
    fn compare() {
        //  BSSes with the same RCPI, RSSI, and protection are equivalent.
        let cfg = ClientConfig::default();
        assert_eq!(
            Ordering::Equal,
            cfg.compare_bss(
                &fake_bss!(Wpa2, rssi_dbm: -10, rcpi_dbmh: -30),
                &fake_bss!(Wpa2, rssi_dbm: -10, rcpi_dbmh: -30)
            )
        );
        // Compatibility takes priority over everything else
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa1, rssi_dbm: -10, rcpi_dbmh: -10),
            &fake_bss!(Wpa2, rssi_dbm: -50, rcpi_dbmh: -50),
        );
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa1, rssi_dbm: -10, rcpi_dbmh: -10),
            &fake_bss!(Wpa2, rssi_dbm: -50, rcpi_dbmh: -50),
        );
        // Higher security is better.
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Open, rssi_dbm: -10, rcpi_dbmh: -10),
            &fake_bss!(Wpa2, rssi_dbm: -50, rcpi_dbmh: -50),
        );

        // RCPI in dBmh takes priority over RSSI in dBmh
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa2, rssi_dbm: -20, rcpi_dbmh: -30),
            &fake_bss!(Wpa2, rssi_dbm: -30, rcpi_dbmh: -20),
        );
        // Compare RSSI if RCPI is absent
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa2, rssi_dbm: -30, rcpi_dbmh: 0),
            &fake_bss!(Wpa2, rssi_dbm: -20, rcpi_dbmh: 0),
        );
        // Having an RCPI measurement is always better than not having any measurement
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa2, rssi_dbm: 0, rcpi_dbmh: 0),
            &fake_bss!(Wpa2, rssi_dbm: 0, rcpi_dbmh: -200),
        );
        // Having an RSSI measurement is always better than not having any measurement
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa2, rssi_dbm: 0, rcpi_dbmh: 0),
            &fake_bss!(Wpa2, rssi_dbm: -100, rcpi_dbmh: 0),
        );
    }

    #[test]
    fn compare_with_wep_supported() {
        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        // WEP is supported while WPA1 is not, so we prefer it.
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wpa1, rssi_dbm: -10, rcpi_dbmh: -10),
            &fake_bss!(Wep, rssi_dbm: -50, rcpi_dbmh: -50),
        );
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wep, rssi_dbm: -10, rcpi_dbmh: -10),
            &fake_bss!(Wpa2, rssi_dbm: -50, rcpi_dbmh: -50),
        );
    }

    #[test]
    fn compare_with_wep_and_wpa1_supported() {
        let cfg = ClientConfig::from_config(Config::default().with_wep().with_wpa1(), false);
        // WEP is worse than WPA1 when both are supported.
        assert_bss_cmp(
            &cfg,
            &fake_bss!(Wep, rssi_dbm: -50, rcpi_dbmh: -50),
            &fake_bss!(Wpa1, rssi_dbm: -10, rcpi_dbmh: -10),
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
        let bss1 = fake_bss!(Wep, rssi_dbm: -30, rcpi_dbmh: -10);
        let bss2 = fake_bss!(Wpa2, rssi_dbm: -20, rcpi_dbmh: -10);
        let bss3 = fake_bss!(Wpa2, rssi_dbm: -80, rcpi_dbmh: -80);
        let bss_list = vec![bss1, bss2, bss3];
        assert_eq!(cfg.get_best_bss(&bss_list), Some(&bss_list[1]));
    }

    #[test]
    fn verify_compatibility() {
        // Compatible:
        let cfg = ClientConfig::default();
        assert!(cfg.is_bss_compatible(&fake_bss!(Open)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa2)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa2Wpa3)));

        // Not compatible:
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa1)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa2Legacy)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa2NoPrivacy)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa3)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Eap)));

        // WEP support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        assert!(cfg.is_bss_compatible(&fake_bss!(Wep)));

        // WPA3 support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default(), true);
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa3)));
    }

    #[test]
    fn convert_bss() {
        let cfg = ClientConfig::default();
        assert_eq!(
            cfg.convert_bss_description(
                &fake_bss!(Wpa2,
                    ssid: vec![],
                    bssid: [0u8; 6],
                    rssi_dbm: -30,
                    snr_db: 0,
                    chan: fidl_common::WlanChan {
                        primary: 1,
                        secondary80: 0,
                        cbw: fidl_common::Cbw::Cbw20,
                    },
                    ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                        bytes: fake_ht_cap_bytes()
                    })),
                    vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                        bytes: fake_vht_cap_bytes()
                    })),
                ),
                None
            ),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -30,
                snr_db: 0,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wpa2Personal,
                compatible: true,
                ht_cap: Some(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );

        let wmm_param = *ie::parse_wmm_param(&fake_wmm_param().bytes[..])
            .expect("expect WMM param to be parseable");
        assert_eq!(
            cfg.convert_bss_description(
                &fake_bss!(Wpa2,
                    ssid: vec![],
                    bssid: [0u8; 6],
                    rssi_dbm: -30,
                    snr_db: 0,
                    chan: fidl_common::WlanChan {
                        primary: 1,
                        secondary80: 0,
                        cbw: fidl_common::Cbw::Cbw20,
                    },
                    ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                        bytes: fake_ht_cap_bytes()
                    })),
                    vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                        bytes: fake_vht_cap_bytes()
                    })),
                ),
                Some(wmm_param)
            ),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -30,
                snr_db: 0,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wpa2Personal,
                compatible: true,
                ht_cap: Some(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: Some(wmm_param),
            }
        );

        assert_eq!(
            cfg.convert_bss_description(
                &fake_bss!(Wep,
                    ssid: vec![],
                    bssid: [0u8; 6],
                    rssi_dbm: -30,
                    snr_db: 0,
                    chan: fidl_common::WlanChan {
                        primary: 1,
                        secondary80: 0,
                        cbw: fidl_common::Cbw::Cbw20,
                    },
                    ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                        bytes: fake_ht_cap_bytes()
                    })),
                    vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                        bytes: fake_vht_cap_bytes()
                    })),
                ),
                None
            ),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -30,
                snr_db: 0,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wep,
                compatible: false,
                ht_cap: Some(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );

        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        assert_eq!(
            cfg.convert_bss_description(
                &fake_bss!(Wep,
                    ssid: vec![],
                    bssid: [0u8; 6],
                    rssi_dbm: -30,
                    snr_db: 0,
                    chan: fidl_common::WlanChan {
                        primary: 1,
                        secondary80: 0,
                        cbw: fidl_common::Cbw::Cbw20,
                    },
                    ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                        bytes: fake_ht_cap_bytes()
                    })),
                    vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                        bytes: fake_vht_cap_bytes()
                    })),
                ),
                None
            ),
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rx_dbm: -30,
                snr_db: 0,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wep,
                compatible: true,
                ht_cap: Some(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
            }
        );
    }

    #[test]
    fn count_ssid_in_bss_list() {
        let cfg = ClientConfig::default();
        let bss1 = fake_bss!(Open, ssid: b"foo".to_vec(), bssid: [1, 1, 1, 1, 1, 1]);
        let bss2 = fake_bss!(Open, ssid: b"bar".to_vec(), bssid: [2, 2, 2, 2, 2, 2]);
        let bss3 = fake_bss!(Open, ssid: b"foo".to_vec(), bssid: [3, 3, 3, 3, 3, 3]);
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
}
