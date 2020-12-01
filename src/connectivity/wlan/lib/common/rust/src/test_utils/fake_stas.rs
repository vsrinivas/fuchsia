// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
pub use fidl_fuchsia_wlan_internal as fidl_internal;

use crate::{
    ie::fake_ies::{fake_ht_cap_bytes, fake_ht_op_bytes, fake_vht_cap_bytes, fake_vht_op_bytes},
    mac,
    test_utils::fake_frames::{
        fake_eap_rsne, fake_wpa1_ie, fake_wpa2_enterprise_rsne, fake_wpa2_legacy_rsne,
        fake_wpa2_mixed_rsne, fake_wpa2_rsne, fake_wpa2_wpa3_rsne,
        fake_wpa3_enterprise_192_bit_rsne, fake_wpa3_rsne,
    },
};

pub enum FakeProtectionCfg__ {
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

pub fn build_fake_bss__(protection_cfg: FakeProtectionCfg__) -> fidl_internal::BssDescription {
    fidl_internal::BssDescription {
        bssid: [7, 1, 2, 77, 53, 8],
        ssid: b"fake-ssid".to_vec(),
        bss_type: fidl_internal::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        rates: vec![0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        country: None,

        ht_cap: Some(Box::new(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() })),
        ht_op: Some(Box::new(fidl_internal::HtOperation { bytes: fake_ht_op_bytes() })),
        vht_cap: Some(Box::new(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() })),
        vht_op: Some(Box::new(fidl_internal::VhtOperation { bytes: fake_vht_op_bytes() })),
        chan: fidl_common::WlanChan { primary: 3, secondary80: 0, cbw: fidl_common::Cbw::Cbw40 },
        rssi_dbm: 0,
        snr_db: 0,

        cap: mac::CapabilityInfo(0)
            .with_privacy(match protection_cfg {
                FakeProtectionCfg__::Open | FakeProtectionCfg__::Wpa2NoPrivacy => false,
                _ => true,
            })
            .0,
        rsne: match protection_cfg {
            FakeProtectionCfg__::Wpa3Enterprise => Some(fake_wpa3_enterprise_192_bit_rsne()),
            FakeProtectionCfg__::Wpa2Enterprise => Some(fake_wpa2_enterprise_rsne()),
            FakeProtectionCfg__::Wpa3 => Some(fake_wpa3_rsne()),
            FakeProtectionCfg__::Wpa2Wpa3 => Some(fake_wpa2_wpa3_rsne()),
            FakeProtectionCfg__::Wpa2Mixed => Some(fake_wpa2_mixed_rsne()),
            FakeProtectionCfg__::Wpa2Legacy => Some(fake_wpa2_legacy_rsne()),
            FakeProtectionCfg__::Wpa1Wpa2
            | FakeProtectionCfg__::Wpa2
            | FakeProtectionCfg__::Wpa2NoPrivacy => Some(fake_wpa2_rsne()),
            FakeProtectionCfg__::Eap => Some(fake_eap_rsne()),
            _ => None,
        },
        vendor_ies: match protection_cfg {
            FakeProtectionCfg__::Wpa1 | FakeProtectionCfg__::Wpa1Wpa2 => Some(fake_wpa1_ie(false)),
            FakeProtectionCfg__::Wpa1Enhanced => Some(fake_wpa1_ie(true)),
            _ => None,
        },
    }
}

#[macro_export]
macro_rules! fake_bss {
    ($protection_type:ident$(, $bss_key:ident: $bss_value:expr)* $(,)?) => {
        $crate::test_utils::fake_stas::fidl_internal::BssDescription {
            $(
                $bss_key: $bss_value,
            )*
                ..$crate::test_utils::fake_stas::build_fake_bss__($crate::test_utils::fake_stas::FakeProtectionCfg__::$protection_type)
        }
    }
}
