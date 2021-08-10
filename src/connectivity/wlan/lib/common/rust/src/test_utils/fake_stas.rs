// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ie::{self, write_wmm_param_v1, IeType},
        mac,
        test_utils::fake_frames::{
            fake_eap_rsne, fake_wpa1_ie, fake_wpa2_enterprise_rsne, fake_wpa2_rsne,
            fake_wpa2_tkip_ccmp_rsne, fake_wpa2_tkip_only_rsne, fake_wpa2_wpa3_rsne,
            fake_wpa3_enterprise_192_bit_rsne, fake_wpa3_rsne, fake_wpa3_transition_rsne,
        },
    },
    anyhow::Context,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_sme as fidl_sme,
    ieee80211::Ssid,
};

#[rustfmt::skip]
const DEFAULT_MOCK_IES: &'static [u8] = &[
    // DS parameter set: channel 140
    0x03, 0x01, 0x8c,
    // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
    0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
    // Country info
    0x07, 0x10, 0x55, 0x53, 0x20, // US, Any environment
    0x24, 0x04, 0x24, // 1st channel: 36, # channels: 4, maximum tx power: 36 dBm
    0x34, 0x04, 0x1e, // 1st channel: 52, # channels: 4, maximum tx power: 30 dBm
    0x64, 0x0c, 0x1e, // 1st channel: 100, # channels: 12, maximum tx power: 30 dBm
    0x95, 0x05, 0x24, // 1st channel: 149, # channels: 5, maximum tx power: 36 dBm
    0x00, // padding
    // Power constraint: 0
    0x20, 0x01, 0x00,
    // TPC Report Transmit Power: 9, Link Margin: 0
    0x23, 0x02, 0x09, 0x00,
    // HT Capabilities
    0x2d, 0x1a, 0xef, 0x09, // HT capabilities info
    0x17, // A-MPDU parameters
    0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // MCS set
    0x00, 0x00, // HT extended capabilities
    0x00, 0x00, 0x00, 0x00, // Transmit beamforming
    0x00, // Antenna selection capabilities
    // HT Operation
    0x3d, 0x16, 0x8c, // Primary channel: 140
    0x0d, // HT info subset - secondary channel above, any channel width, RIFS permitted
    0x16, 0x00, 0x00, 0x00, // HT info subsets
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // Basic MCS set
    // Extended Capabilities: extended channel switching, BSS transition, operating mode notification
    0x7f, 0x08, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40,
    // VHT Capabilities
    0xbf, 0x0c, 0x91, 0x59, 0x82, 0x0f, // VHT capabilities info
    0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT supported MCS set
    // VHT Operation
    0xc0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    // VHT Tx Power Envelope
    0xc3, 0x03, 0x01, 0x24, 0x24,
    // Aruba, Hewlett Packard vendor-specific IE
    0xdd, 0x07, 0x00, 0x0b, 0x86, 0x01, 0x04, 0x08, 0x09,
];

pub struct BssDescriptionCreator {
    // *** Fields already in fidl_internal::BssDescription
    pub bssid: [u8; 6],
    pub bss_type: fidl_internal::BssType,
    pub beacon_period: u16,
    pub timestamp: u64,
    pub local_time: u64,
    pub channel: fidl_common::WlanChannel,
    pub rssi_dbm: i8,
    pub snr_db: i8,

    // *** Custom arguments
    pub protection_cfg: FakeProtectionCfg,
    pub ssid: Ssid,
    pub rates: Vec<u8>,
    pub wmm_param: Option<ie::WmmParam>,

    // *** Modifiable capability_info bits
    // The privacy, ess, and ibss bits are reserved for the
    // macro to set since they are implied by protection_cfg
    // and bss_type.
    pub cf_pollable: bool,
    pub cf_poll_req: bool,
    pub short_preamble: bool,
    pub spectrum_mgmt: bool,
    pub qos: bool,
    pub short_slot_time: bool,
    pub apsd: bool,
    pub radio_measurement: bool,
    pub delayed_block_ack: bool,
    pub immediate_block_ack: bool,

    pub ies_overrides: IesOverrides,
}

impl BssDescriptionCreator {
    pub fn create_bss_description(self) -> Result<fidl_internal::BssDescription, anyhow::Error> {
        let mut ies_updater = ie::IesUpdater::new(DEFAULT_MOCK_IES.to_vec());
        ies_updater.set(IeType::SSID, &self.ssid[..]).context("set SSID")?;

        let rates_writer = ie::RatesWriter::try_new(&self.rates[..]).context("set rates")?;
        let mut rates_buf = vec![];
        rates_writer.write_supported_rates(&mut rates_buf);
        ies_updater.set_raw(&rates_buf[..]).context("set rates")?;

        let mut ext_rates_buf = vec![];
        rates_writer.write_ext_supported_rates(&mut ext_rates_buf);
        ies_updater.set_raw(&ext_rates_buf[..]).context("set extended rates")?;

        if let Some(rsne) = derive_rsne(self.protection_cfg) {
            ies_updater.set_raw(&rsne[..]).context("set RSNE")?;
        }
        if let Some(wpa1_vendor_ie) = derive_wpa1_vendor_ies(self.protection_cfg) {
            ies_updater.set_raw(&wpa1_vendor_ie[..]).context("set WPA1 vendor IE")?;
        }

        if let Some(wmm_param) = self.wmm_param {
            let mut wmm_param_vendor_ie = vec![];
            write_wmm_param_v1(&mut wmm_param_vendor_ie, &wmm_param)
                .context("failed to write WmmParam to vendor IE buffer")?;
            ies_updater.set_raw(&wmm_param_vendor_ie[..]).context("set WMM parameter IE")?;
        }

        let capability_info = mac::CapabilityInfo(0)
            .with_cf_pollable(self.cf_pollable)
            .with_cf_poll_req(self.cf_poll_req)
            .with_short_preamble(self.short_preamble)
            .with_spectrum_mgmt(self.spectrum_mgmt)
            .with_qos(self.qos)
            .with_short_slot_time(self.short_slot_time)
            .with_apsd(self.apsd)
            .with_radio_measurement(self.radio_measurement)
            .with_delayed_block_ack(self.delayed_block_ack)
            .with_immediate_block_ack(self.immediate_block_ack);

        // Some values of capability_info are not permitted to be set by
        // the macro since otherwise the BssDescription will be trivially invalid.
        let capability_info = match self.protection_cfg {
            FakeProtectionCfg::Open => capability_info.with_privacy(false),
            _ => capability_info.with_privacy(true),
        };
        let capability_info = match self.bss_type {
            fidl_internal::BssType::Infrastructure => {
                capability_info.with_ess(true).with_ibss(false)
            }
            _ => panic!("{:?} is not supported", self.bss_type),
        };
        let capability_info = capability_info.0;

        for ovr in self.ies_overrides.overrides {
            match ovr {
                IeOverride::Remove(ie_type) => ies_updater.remove(&ie_type),
                IeOverride::Set(ie_type, bytes) => {
                    ies_updater
                        .set(ie_type, &bytes[..])
                        .with_context(|| format!("set IE type: {:?}", ie_type))?;
                }
                IeOverride::SetRaw(bytes) => {
                    ies_updater.set_raw(&bytes[..]).context("set raw IE")?;
                }
            }
        }

        Ok(fidl_internal::BssDescription {
            bssid: self.bssid,
            bss_type: self.bss_type,
            beacon_period: self.beacon_period,
            timestamp: self.timestamp,
            local_time: self.local_time,
            capability_info,
            ies: ies_updater.finalize(),
            channel: self.channel,
            rssi_dbm: self.rssi_dbm,
            snr_db: self.snr_db,
        })
    }
}

pub struct IesOverrides {
    overrides: Vec<IeOverride>,
}

impl IesOverrides {
    pub fn new() -> Self {
        Self { overrides: vec![] }
    }

    pub fn remove(mut self, ie_type: IeType) -> Self {
        self.overrides.push(IeOverride::Remove(ie_type));
        self
    }

    pub fn set(mut self, ie_type: IeType, bytes: Vec<u8>) -> Self {
        self.overrides.push(IeOverride::Set(ie_type, bytes));
        self
    }

    pub fn set_raw(mut self, bytes: Vec<u8>) -> Self {
        self.overrides.push(IeOverride::SetRaw(bytes));
        self
    }
}

enum IeOverride {
    Remove(IeType),
    Set(IeType, Vec<u8>),
    SetRaw(Vec<u8>),
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum FakeProtectionCfg {
    Open,
    Wep,
    Wpa1,
    Wpa1Enhanced,
    Wpa1Wpa2TkipOnly,
    Wpa2TkipOnly,
    Wpa1Wpa2,
    Wpa2TkipCcmp,
    Wpa2Enterprise,
    Wpa2,
    Wpa2Wpa3,
    Wpa3Transition,
    Wpa3,
    Wpa3Enterprise,
    Eap,
}

impl From<fidl_sme::Protection> for FakeProtectionCfg {
    fn from(protection: fidl_sme::Protection) -> Self {
        match protection {
            fidl_sme::Protection::Unknown => panic!("unknown protection"),
            fidl_sme::Protection::Open => FakeProtectionCfg::Open,
            fidl_sme::Protection::Wep => FakeProtectionCfg::Wep,
            fidl_sme::Protection::Wpa1 => FakeProtectionCfg::Wpa1,
            fidl_sme::Protection::Wpa1Wpa2PersonalTkipOnly => FakeProtectionCfg::Wpa1Wpa2TkipOnly,
            fidl_sme::Protection::Wpa2PersonalTkipOnly => FakeProtectionCfg::Wpa2TkipOnly,
            fidl_sme::Protection::Wpa1Wpa2Personal => FakeProtectionCfg::Wpa1Wpa2,
            fidl_sme::Protection::Wpa2Personal => FakeProtectionCfg::Wpa2,
            fidl_sme::Protection::Wpa2Wpa3Personal => FakeProtectionCfg::Wpa2Wpa3,
            fidl_sme::Protection::Wpa3Personal => FakeProtectionCfg::Wpa3,
            fidl_sme::Protection::Wpa2Enterprise => FakeProtectionCfg::Wpa2Enterprise,
            fidl_sme::Protection::Wpa3Enterprise => FakeProtectionCfg::Wpa3Enterprise,
        }
    }
}

pub fn build_fake_bss_description_creator__(
    protection_cfg: FakeProtectionCfg,
) -> BssDescriptionCreator {
    BssDescriptionCreator {
        bssid: [7, 1, 2, 77, 53, 8],
        bss_type: fidl_internal::BssType::Infrastructure,
        beacon_period: 100,
        timestamp: 0,
        local_time: 0,
        channel: fidl_common::WlanChannel {
            primary: 3,
            secondary80: 0,
            cbw: fidl_common::ChannelBandwidth::Cbw40,
        },
        rssi_dbm: 0,
        snr_db: 0,
        protection_cfg,
        ssid: (*b"fake-ssid").into(),
        rates: vec![0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        wmm_param: Some(ie::WmmParam {
            wmm_info: ie::WmmInfo(0).with_ap_wmm_info(ie::ApWmmInfo(0).with_uapsd(true)),
            _reserved: 0,
            ac_be_params: ie::WmmAcParams {
                aci_aifsn: ie::WmmAciAifsn(0).with_aifsn(3).with_aci(0),
                ecw_min_max: ie::EcwMinMax(0).with_ecw_min(4).with_ecw_max(10),
                txop_limit: 0,
            },
            ac_bk_params: ie::WmmAcParams {
                aci_aifsn: ie::WmmAciAifsn(0).with_aifsn(7).with_aci(1),
                ecw_min_max: ie::EcwMinMax(0).with_ecw_min(4).with_ecw_max(10),
                txop_limit: 0,
            },
            ac_vi_params: ie::WmmAcParams {
                aci_aifsn: ie::WmmAciAifsn(0).with_aifsn(2).with_aci(2),
                ecw_min_max: ie::EcwMinMax(0).with_ecw_min(3).with_ecw_max(4),
                txop_limit: 94,
            },
            ac_vo_params: ie::WmmAcParams {
                aci_aifsn: ie::WmmAciAifsn(0).with_aifsn(2).with_aci(3),
                ecw_min_max: ie::EcwMinMax(0).with_ecw_min(2).with_ecw_max(3),
                txop_limit: 47,
            },
        }),

        cf_pollable: false,
        cf_poll_req: false,
        short_preamble: false,
        spectrum_mgmt: false,
        qos: false,
        short_slot_time: false,
        apsd: false,
        radio_measurement: false,
        delayed_block_ack: false,
        immediate_block_ack: false,

        ies_overrides: IesOverrides::new(),
    }
}

fn derive_rsne(protection_cfg: FakeProtectionCfg) -> Option<Vec<u8>> {
    match protection_cfg {
        FakeProtectionCfg::Wpa3Enterprise => Some(fake_wpa3_enterprise_192_bit_rsne()),
        FakeProtectionCfg::Wpa2Enterprise => Some(fake_wpa2_enterprise_rsne()),
        FakeProtectionCfg::Wpa3 => Some(fake_wpa3_rsne()),
        FakeProtectionCfg::Wpa3Transition => Some(fake_wpa3_transition_rsne()),
        FakeProtectionCfg::Wpa2Wpa3 => Some(fake_wpa2_wpa3_rsne()),
        FakeProtectionCfg::Wpa2TkipCcmp => Some(fake_wpa2_tkip_ccmp_rsne()),
        FakeProtectionCfg::Wpa1Wpa2TkipOnly | FakeProtectionCfg::Wpa2TkipOnly => {
            Some(fake_wpa2_tkip_only_rsne())
        }
        FakeProtectionCfg::Wpa1Wpa2 | FakeProtectionCfg::Wpa2 => Some(fake_wpa2_rsne()),
        FakeProtectionCfg::Eap => Some(fake_eap_rsne()),
        _ => None,
    }
}

fn derive_wpa1_vendor_ies(protection_cfg: FakeProtectionCfg) -> Option<Vec<u8>> {
    match protection_cfg {
        FakeProtectionCfg::Wpa1
        | FakeProtectionCfg::Wpa1Wpa2TkipOnly
        | FakeProtectionCfg::Wpa1Wpa2 => Some(fake_wpa1_ie(false)),
        FakeProtectionCfg::Wpa1Enhanced => Some(fake_wpa1_ie(true)),
        _ => None,
    }
}

#[macro_export]
macro_rules! fake_fidl_bss_description {
    ($protection_type:ident $(, $bss_key:ident: $bss_value:expr)* $(,)?) => {{
        let protection = $crate::test_utils::fake_stas::FakeProtectionCfg::$protection_type;
        $crate::fake_fidl_bss_description!(protection => protection $(, $bss_key: $bss_value)*)
    }};
    (protection => $protection_type:expr $(, $bss_key:ident: $bss_value:expr)* $(,)?) => {{
        let bss_description_creator = $crate::test_utils::fake_stas::BssDescriptionCreator {
            $(
                $bss_key: $bss_value,
            )*
            ..$crate::test_utils::fake_stas::build_fake_bss_description_creator__($protection_type.into())
        };
        bss_description_creator.create_bss_description().expect("expect creating BSS to succeed")
    }};
}

#[macro_export]
macro_rules! fake_bss_description {
    ($protection_type:ident $(, $bss_key:ident: $bss_value:expr)* $(,)?) => {{
        let fidl_bss = $crate::fake_fidl_bss_description!($protection_type $(, $bss_key: $bss_value)*);
        let bss_description = $crate::bss::BssDescription::from_fidl(fidl_bss)
            .expect("expect BSS conversion to succeed");
        bss_description
    }}
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::bss::{BssDescription, Protection},
        ie::IeType,
    };

    #[test]
    fn macro_sanity_test() {
        let fidl_bss_description = fake_fidl_bss_description!(Open);
        let bss_description = fake_bss_description!(Open);
        assert_eq!(
            BssDescription::from_fidl(fidl_bss_description)
                .expect("Failed to convert fake_fidl_bss_description value"),
            bss_description
        );
    }

    #[test]
    fn fake_protection_cfg_privacy_bit_and_protection() {
        let bss = fake_bss_description!(Open);
        assert!(!mac::CapabilityInfo(bss.capability_info).privacy());
        assert_eq!(bss.protection(), Protection::Open);

        let bss = fake_bss_description!(Wep);
        assert!(mac::CapabilityInfo(bss.capability_info).privacy());
        assert_eq!(bss.protection(), Protection::Wep);

        let bss = fake_bss_description!(Wpa1);
        assert!(mac::CapabilityInfo(bss.capability_info).privacy());
        assert_eq!(bss.protection(), Protection::Wpa1);

        let bss = fake_bss_description!(Wpa2);
        assert!(mac::CapabilityInfo(bss.capability_info).privacy());
        assert_eq!(bss.protection(), Protection::Wpa2Personal);
    }

    #[test]
    fn set_capability_info_bits() {
        macro_rules! check_bit {
            ($bit_name:ident) => {{
                let bss = fake_bss_description!(Open, $bit_name: true);
                assert!(mac::CapabilityInfo(bss.capability_info).$bit_name());
                let bss = fake_bss_description!(Open, $bit_name: false);
                assert!(!mac::CapabilityInfo(bss.capability_info).$bit_name());
            }}
        }
        check_bit!(cf_pollable);
        check_bit!(cf_poll_req);
        check_bit!(short_preamble);
        check_bit!(spectrum_mgmt);
        check_bit!(qos);
        check_bit!(short_slot_time);
        check_bit!(apsd);
        check_bit!(radio_measurement);
        check_bit!(delayed_block_ack);
        check_bit!(immediate_block_ack);

        let bss =
            fake_bss_description!(Open, cf_pollable: true, apsd: false, immediate_block_ack: true);
        assert!(mac::CapabilityInfo(bss.capability_info).cf_pollable());
        assert!(!mac::CapabilityInfo(bss.capability_info).apsd());
        assert!(mac::CapabilityInfo(bss.capability_info).immediate_block_ack());
    }

    #[test]
    fn simple_default_override() {
        let bss = fake_fidl_bss_description!(Open);
        assert_eq!(bss.beacon_period, 100);

        let bss = fake_fidl_bss_description!(Open, beacon_period: 50);
        assert_eq!(bss.beacon_period, 50);
    }

    #[test]
    #[should_panic(expected = "Personal is not supported")]
    fn unsupported_bss_type() {
        fake_fidl_bss_description!(Open, bss_type: fidl_internal::BssType::Personal);
    }

    #[test]
    fn ies_overrides() {
        let bss = fake_bss_description!(Wpa1Wpa2,
            ssid: Ssid::from("fuchsia"),
            rates: vec![11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24],
            ies_overrides: IesOverrides::new()
                .remove(IeType::new_vendor([0x00, 0x0b, 0x86, 0x01, 0x04, 0x08]))
                .set(IeType::DSSS_PARAM_SET, [136].to_vec()),
        );

        // Things to note:
        // - SSID "fuchsia" is inserted.
        // - Rates and extended supported rates are inserted.
        // - WPA2 RSNE and WPA1 vendor IE are inserted.
        // - DSSS Param set's value is changed.
        // - Aruba vendor IE no longer there.
        #[rustfmt::skip]
        let expected_ies = vec![
            // SSID
            0x00, 0x07, b'f', b'u', b'c', b'h', b's', b'i', b'a',
            // Rates
            0x01, 0x08, 11, 12, 13, 14, 15, 16, 17, 18,
            // DS parameter set: channel 136
            0x03, 0x01, 136,
            // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
            0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
            // Country info
            0x07, 0x10, 0x55, 0x53, 0x20, // US, Any environment
            0x24, 0x04, 0x24, // 1st channel: 36, # channels: 4, maximum tx power: 36 dBm
            0x34, 0x04, 0x1e, // 1st channel: 52, # channels: 4, maximum tx power: 30 dBm
            0x64, 0x0c, 0x1e, // 1st channel: 100, # channels: 12, maximum tx power: 30 dBm
            0x95, 0x05, 0x24, // 1st channel: 149, # channels: 5, maximum tx power: 36 dBm
            0x00, // padding
            // Power constraint: 0
            0x20, 0x01, 0x00,
            // TPC Report Transmit Power: 9, Link Margin: 0
            0x23, 0x02, 0x09, 0x00,
            // HT Capabilities
            0x2d, 0x1a, 0xef, 0x09, // HT capabilities info
            0x17, // A-MPDU parameters
            0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, // MCS set
            0x00, 0x00, // HT extended capabilities
            0x00, 0x00, 0x00, 0x00, // Transmit beamforming
            0x00, // Antenna selection capabilities
            // RSNE
            0x30, 18, // Element header
            1, 0, // Version
            0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
            1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
            // Extended supported rates
            0x32, 0x06, 19, 20, 21, 22, 23, 24,
            // HT Operation
            0x3d, 0x16, 0x8c, // Primary channel: 140
            0x0d, // HT info subset - secondary channel above, any channel width, RIFS permitted
            0x16, 0x00, 0x00, 0x00, // HT info subsets
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, // Basic MCS set
            // Extended Capabilities: extended channel switching, BSS transition, operating mode notification
            0x7f, 0x08, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40,
            // VHT Capabilities
            0xbf, 0x0c, 0x91, 0x59, 0x82, 0x0f, // VHT capabilities info
            0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT supported MCS set
            // VHT Operation
            0xc0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, // VHT Tx Power Envelope
            0xc3, 0x03, 0x01, 0x24, 0x24,
            // WPA1 vendor IE
            0xdd, 0x16, 0x00, 0x50, 0xf2, // IE header
            0x01, // MSFT specific IE type (WPA)
            0x01, 0x00, // WPA version
            0x00, 0x50, 0xf2, 0x02, // multicast cipher: TKIP
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
            // WMM parameters
            0xdd, 0x18, // Vendor IE header
            0x00, 0x50, 0xf2, // MSFT OUI
            0x02, 0x01, // WMM OUI and WMM Parameter OUI Subtype
            0x01, // Version 1
            0x80, // U-APSD enabled
            0x00, // reserved
            0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
            0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
            0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
            0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        ];
        assert_eq!(bss.ies, expected_ies);
    }
}
