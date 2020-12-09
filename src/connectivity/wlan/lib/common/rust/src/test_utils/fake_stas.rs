// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        bss::BssDescription,
        ie::fake_ies::{
            fake_ht_cap_bytes, fake_ht_op_bytes, fake_vht_cap_bytes, fake_vht_op_bytes,
        },
        mac,
        test_utils::fake_frames::{
            fake_eap_rsne, fake_wpa1_ie, fake_wpa2_enterprise_rsne, fake_wpa2_legacy_rsne,
            fake_wpa2_mixed_rsne, fake_wpa2_rsne, fake_wpa2_wpa3_rsne,
            fake_wpa3_enterprise_192_bit_rsne, fake_wpa3_rsne,
        },
    },
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    std::convert::TryInto,
};

pub fn fake_fidl_bss(
    protection_cfg: FakeProtectionCfg,
    ssid: Vec<u8>,
) -> fidl_internal::BssDescription {
    let mut ies: Vec<u8> = vec![0x00, ssid.len().try_into().unwrap()];
    ies.extend(ssid);
    ies.extend(vec![
        // Supported rates: 24(B), 36, 48, 54
        0x01, 0x04, 0xb0, 0x48, 0x60, 0x6c, // DS parameter set: channel 140
        0x03, 0x01, 0x8c, // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
        0x05, 0x04, 0x00, 0x01, 0x00, 0x02, // Country info
        0x07, 0x10, 0x55, 0x53, 0x20, // US, Any environment
        0x24, 0x04, 0x24, // 1st channel: 36, # channels: 4, maximum tx power: 36 dBm
        0x34, 0x04, 0x1e, // 1st channel: 52, # channels: 4, maximum tx power: 30 dBm
        0x64, 0x0c, 0x1e, // 1st channel: 100, # channels: 12, maximum tx power: 30 dBm
        0x95, 0x05, 0x24, // 1st channel: 149, # channels: 5, maximum tx power: 36 dBm
        0x00, // padding
        // Power constraint: 0
        0x20, 0x01, 0x00, // TPC Report Transmit Power: 9, Link Margin: 0
        0x23, 0x02, 0x09, 0x00,
    ]);
    ies.extend(derive_rsne(protection_cfg).unwrap_or(vec![]));
    ies.extend(vec![
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
        0x7f, 0x08, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40, // VHT Capabilities
        0xbf, 0x0c, 0x91, 0x59, 0x82, 0x0f, // VHT capabilities info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT supported MCS set
        // VHT Operation
        0xc0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, // VHT Tx Power Envelope
        0xc3, 0x03, 0x01, 0x24, 0x24,
    ]);
    ies.extend(derive_wpa1_vendor_ies(protection_cfg).unwrap_or(vec![]));
    ies.extend(vec![
        // Aruba, Hewlett Packard vendor-specific IE
        0xdd, 0x07, 0x00, 0x0b, 0x86, 0x01, 0x04, 0x08, 0x09, // WMM parameters
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, // U-APSD enabled
        0x00, // reserved
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
    ]);

    fidl_internal::BssDescription {
        bssid: [7, 1, 2, 77, 53, 8],
        bss_type: fidl_internal::BssTypes::Infrastructure,
        beacon_period: 100,
        timestamp: 0,
        local_time: 0,

        chan: fidl_common::WlanChan { primary: 3, secondary80: 0, cbw: fidl_common::Cbw::Cbw40 },
        rssi_dbm: 0,
        snr_db: 0,

        cap: mac::CapabilityInfo(0)
            .with_privacy(match protection_cfg {
                FakeProtectionCfg::Open | FakeProtectionCfg::Wpa2NoPrivacy => false,
                _ => true,
            })
            .0,
        ies,
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum FakeProtectionCfg {
    Open,
    Wep,
    Wpa1,
    Wpa1Enhanced,
    Wpa2Legacy,
    Wpa1Wpa2,
    Wpa2Mixed,
    Wpa2Enterprise,
    Wpa2,
    Wpa2Wpa3,
    Wpa3,
    Wpa3Enterprise,
    Wpa2NoPrivacy,
    Eap,
}

pub fn build_fake_bss__(protection_cfg: FakeProtectionCfg) -> BssDescription {
    BssDescription {
        bssid: [7, 1, 2, 77, 53, 8],
        ssid: b"fake-ssid".to_vec(),
        bss_type: fidl_internal::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        rates: vec![0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        country: None,

        ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
        ht_op: Some(fidl_internal::HtOperation { bytes: fake_ht_op_bytes() }),
        vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
        vht_op: Some(fidl_internal::VhtOperation { bytes: fake_vht_op_bytes() }),
        chan: fidl_common::WlanChan { primary: 3, secondary80: 0, cbw: fidl_common::Cbw::Cbw40 },
        rssi_dbm: 0,
        snr_db: 0,

        cap: mac::CapabilityInfo(0)
            .with_privacy(match protection_cfg {
                FakeProtectionCfg::Open | FakeProtectionCfg::Wpa2NoPrivacy => false,
                _ => true,
            })
            .0,
        rsne: derive_rsne(protection_cfg),
        ies: derive_wpa1_vendor_ies(protection_cfg).unwrap_or(vec![]),
    }
}

fn derive_rsne(protection_cfg: FakeProtectionCfg) -> Option<Vec<u8>> {
    match protection_cfg {
        FakeProtectionCfg::Wpa3Enterprise => Some(fake_wpa3_enterprise_192_bit_rsne()),
        FakeProtectionCfg::Wpa2Enterprise => Some(fake_wpa2_enterprise_rsne()),
        FakeProtectionCfg::Wpa3 => Some(fake_wpa3_rsne()),
        FakeProtectionCfg::Wpa2Wpa3 => Some(fake_wpa2_wpa3_rsne()),
        FakeProtectionCfg::Wpa2Mixed => Some(fake_wpa2_mixed_rsne()),
        FakeProtectionCfg::Wpa2Legacy => Some(fake_wpa2_legacy_rsne()),
        FakeProtectionCfg::Wpa1Wpa2
        | FakeProtectionCfg::Wpa2
        | FakeProtectionCfg::Wpa2NoPrivacy => Some(fake_wpa2_rsne()),
        FakeProtectionCfg::Eap => Some(fake_eap_rsne()),
        _ => None,
    }
}

fn derive_wpa1_vendor_ies(protection_cfg: FakeProtectionCfg) -> Option<Vec<u8>> {
    match protection_cfg {
        FakeProtectionCfg::Wpa1 | FakeProtectionCfg::Wpa1Wpa2 => Some(fake_wpa1_ie(false)),
        FakeProtectionCfg::Wpa1Enhanced => Some(fake_wpa1_ie(true)),
        _ => None,
    }
}

#[macro_export]
macro_rules! fake_bss {
    ($protection_type:ident$(, $bss_key:ident: $bss_value:expr)* $(,)?) => {
        $crate::bss::BssDescription {
            $(
                $bss_key: $bss_value,
            )*
                ..$crate::test_utils::fake_stas::build_fake_bss__($crate::test_utils::fake_stas::FakeProtectionCfg::$protection_type)
        }
    }
}
