// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    crate::client::types,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_zircon as zx,
    ieee80211::{Bssid, Ssid},
    rand::Rng as _,
    std::convert::TryFrom,
    wlan_common::random_fidl_bss_description,
};

pub fn generate_random_channel() -> fidl_common::WlanChannel {
    let mut rng = rand::thread_rng();
    generate_channel(rng.gen::<u8>())
}

pub fn generate_channel(channel: u8) -> fidl_common::WlanChannel {
    let mut rng = rand::thread_rng();
    fidl_common::WlanChannel {
        primary: channel,
        cbw: match rng.gen_range(0, 5) {
            0 => fidl_common::ChannelBandwidth::Cbw20,
            1 => fidl_common::ChannelBandwidth::Cbw40,
            2 => fidl_common::ChannelBandwidth::Cbw40Below,
            3 => fidl_common::ChannelBandwidth::Cbw80,
            4 => fidl_common::ChannelBandwidth::Cbw160,
            5 => fidl_common::ChannelBandwidth::Cbw80P80,
            _ => panic!(),
        },
        secondary80: rng.gen::<u8>(),
    }
}

pub fn generate_random_sme_scan_result() -> fidl_sme::ScanResult {
    let mut rng = rand::thread_rng();
    fidl_sme::ScanResult {
        compatible: rng.gen::<bool>(),
        timestamp_nanos: rng.gen(),
        bss_description: random_fidl_bss_description!(),
    }
}

pub fn generate_random_bss() -> types::Bss {
    let mut rng = rand::thread_rng();
    let bssid: Bssid = Bssid(rng.gen());
    let rssi = rng.gen_range(-100, 20);
    let channel = generate_random_channel();
    let timestamp = zx::Time::from_nanos(rng.gen());
    let snr_db = rng.gen_range(-20, 50);

    types::Bss {
        bssid,
        rssi,
        channel,
        timestamp,
        snr_db,
        observed_in_passive_scan: rng.gen::<bool>(),
        compatible: rng.gen::<bool>(),
        bss_description: random_fidl_bss_description!(
            bssid: bssid.0,
            rssi_dbm: rssi,
            channel: channel,
            snr_db: snr_db,
        ),
    }
}

pub fn generate_random_scan_result() -> types::ScanResult {
    let mut rng = rand::thread_rng();
    let ssid = Ssid::try_from(format!("scan result rand {}", rng.gen::<i32>()))
        .expect("Failed to create random SSID from String");
    types::ScanResult {
        ssid,
        security_type_detailed: types::SecurityTypeDetailed::Wpa1,
        entries: vec![generate_random_bss(), generate_random_bss()],
        compatibility: match rng.gen_range(0, 2) {
            0 => types::Compatibility::Supported,
            1 => types::Compatibility::DisallowedNotSupported,
            2 => types::Compatibility::DisallowedInsecure,
            _ => panic!(),
        },
    }
}

pub fn generate_disconnect_info(is_sme_reconnecting: bool) -> fidl_sme::DisconnectInfo {
    let mut rng = rand::thread_rng();
    fidl_sme::DisconnectInfo {
        is_sme_reconnecting,
        reason_code: rng.gen::<u16>(),
        disconnect_source: match rng.gen_range(0, 2) {
            0 => fidl_sme::DisconnectSource::Ap,
            1 => fidl_sme::DisconnectSource::User,
            2 => fidl_sme::DisconnectSource::Mlme,
            _ => panic!(),
        },
    }
}
