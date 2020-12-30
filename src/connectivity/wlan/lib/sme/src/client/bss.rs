// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{Config, Ssid},
    fidl_fuchsia_wlan_internal as fidl_internal, fuchsia_zircon as zx,
    wlan_common::{
        bss::{BssDescription, Protection},
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
            ssid: bss.ssid().to_vec(),
            rssi_dbm: bss.rssi_dbm,
            snr_db: bss.snr_db,
            signal_report_time: zx::Time::ZERO,
            channel: Channel::from_fidl(bss.chan),
            protection: bss.protection(),
            compatible: self.is_bss_compatible(bss),
            ht_cap: bss.raw_ht_cap(),
            vht_cap: bss.raw_vht_cap(),
            probe_resp_wsc,
            wmm_param,
            bss_desc: Some(bss.clone().to_fidl()),
        }
    }

    /// Determines whether a given BSS is compatible with this client SME configuration.
    pub fn is_bss_compatible(&self, bss: &BssDescription) -> bool {
        let privacy = wlan_common::mac::CapabilityInfo(bss.cap).privacy();
        let protection = bss.protection();
        match &protection {
            Protection::Open => true,
            Protection::Wep => self.cfg.wep_supported,
            Protection::Wpa1 => self.cfg.wpa1_supported,
            Protection::Wpa2Wpa3Personal | Protection::Wpa3Personal if self.wpa3_supported => {
                match bss.rsne() {
                    Some(rsne) if privacy => match rsne::from_bytes(rsne) {
                        Ok((_, a_rsne)) => a_rsne.is_wpa3_rsn_compatible(),
                        _ => false,
                    },
                    _ => false,
                }
            }
            Protection::Wpa2Legacy
            | Protection::Wpa1Wpa2Personal
            | Protection::Wpa2Personal
            | Protection::Wpa2Wpa3Personal => match bss.rsne() {
                Some(rsne) if privacy => match rsne::from_bytes(rsne) {
                    Ok((_, a_rsne)) => a_rsne.is_wpa2_rsn_compatible(),
                    _ => false,
                },
                _ => false,
            },
            _ => false,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct BssInfo {
    pub bssid: [u8; 6],
    pub ssid: Ssid,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    pub signal_report_time: zx::Time,
    pub channel: wlan_common::channel::Channel,
    pub protection: Protection,
    pub compatible: bool,
    pub ht_cap: Option<fidl_internal::HtCapabilities>,
    pub vht_cap: Option<fidl_internal::VhtCapabilities>,
    pub probe_resp_wsc: Option<wsc::ProbeRespWsc>,
    pub wmm_param: Option<ie::WmmParam>,
    pub bss_desc: Option<fidl_internal::BssDescription>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::test_utils::fake_wmm_param,
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::{
            channel::Cbw,
            fake_bss,
            ie::{
                self,
                fake_ies::{fake_ht_cap_bytes, fake_vht_cap_bytes},
                IeType,
            },
            test_utils::fake_stas::IesOverrides,
        },
    };

    #[test]
    fn verify_compatibility() {
        // Compatible:
        let cfg = ClientConfig::default();
        assert!(cfg.is_bss_compatible(&fake_bss!(Open)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa2Legacy)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa2)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa2Wpa3)));

        // Not compatible:
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa1)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa2NoPrivacy)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa3)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Wpa3Transition)));
        assert!(!cfg.is_bss_compatible(&fake_bss!(Eap)));

        // WEP support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        assert!(cfg.is_bss_compatible(&fake_bss!(Wep)));

        // WPA3 support is configurable to be on or off:
        let cfg = ClientConfig::from_config(Config::default(), true);
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa3)));
        assert!(cfg.is_bss_compatible(&fake_bss!(Wpa3Transition)));
    }

    #[test]
    fn convert_bss() {
        let cfg = ClientConfig::default();
        let bss_desc = fake_bss!(Wpa2,
            ssid: vec![],
            bssid: [0u8; 6],
            rssi_dbm: -30,
            snr_db: 0,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
            ies_overrides: IesOverrides::new()
                .set(IeType::HT_CAPABILITIES, fake_ht_cap_bytes().to_vec())
                .set(IeType::VHT_CAPABILITIES, fake_vht_cap_bytes().to_vec()),
        );
        let bss_info = cfg.convert_bss_description(&bss_desc, None);

        assert_eq!(
            bss_info,
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rssi_dbm: -30,
                snr_db: 0,
                signal_report_time: zx::Time::ZERO,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wpa2Personal,
                compatible: true,
                ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
                bss_desc: Some(bss_desc.to_fidl()),
            }
        );

        let wmm_param = *ie::parse_wmm_param(&fake_wmm_param().bytes[..])
            .expect("expect WMM param to be parseable");
        let bss_desc = fake_bss!(Wpa2,
            ssid: vec![],
            bssid: [0u8; 6],
            rssi_dbm: -30,
            snr_db: 0,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
            ies_overrides: IesOverrides::new()
                .set(IeType::HT_CAPABILITIES, fake_ht_cap_bytes().to_vec())
                .set(IeType::VHT_CAPABILITIES, fake_vht_cap_bytes().to_vec()),
        );
        let bss_info = cfg.convert_bss_description(&bss_desc, Some(wmm_param));

        assert_eq!(
            bss_info,
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rssi_dbm: -30,
                snr_db: 0,
                signal_report_time: zx::Time::ZERO,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wpa2Personal,
                compatible: true,
                ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: Some(wmm_param),
                bss_desc: Some(bss_desc.to_fidl()),
            }
        );

        let bss_desc = fake_bss!(Wep,
            ssid: vec![],
            bssid: [0u8; 6],
            rssi_dbm: -30,
            snr_db: 0,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
            ies_overrides: IesOverrides::new()
                .set(IeType::HT_CAPABILITIES, fake_ht_cap_bytes().to_vec())
                .set(IeType::VHT_CAPABILITIES, fake_vht_cap_bytes().to_vec()),
        );
        let bss_info = cfg.convert_bss_description(&bss_desc, None);
        assert_eq!(
            bss_info,
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rssi_dbm: -30,
                snr_db: 0,
                signal_report_time: zx::Time::ZERO,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wep,
                compatible: false,
                ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
                bss_desc: Some(bss_desc.to_fidl()),
            },
        );

        let cfg = ClientConfig::from_config(Config::default().with_wep(), false);
        let bss_desc = fake_bss!(Wep,
            ssid: vec![],
            bssid: [0u8; 6],
            rssi_dbm: -30,
            snr_db: 0,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
            ies_overrides: IesOverrides::new()
                .set(IeType::HT_CAPABILITIES, fake_ht_cap_bytes().to_vec())
                .set(IeType::VHT_CAPABILITIES, fake_vht_cap_bytes().to_vec()),
        );
        let bss_info = cfg.convert_bss_description(&bss_desc, None);
        assert_eq!(
            bss_info,
            BssInfo {
                bssid: [0u8; 6],
                ssid: vec![],
                rssi_dbm: -30,
                snr_db: 0,
                signal_report_time: zx::Time::ZERO,
                channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
                protection: Protection::Wep,
                compatible: true,
                ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
                vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
                probe_resp_wsc: None,
                wmm_param: None,
                bss_desc: Some(bss_desc.to_fidl()),
            },
        );
    }
}
