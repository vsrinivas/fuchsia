// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    crate::client::types, fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_sme as fidl_sme, rand::Rng as _, wlan_common::random_fidl_bss_description,
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
        bss_description: random_fidl_bss_description!(),
    }
}

pub fn generate_random_bss() -> types::Bss {
    let mut rng = rand::thread_rng();
    let bssid: types::Bssid = rng.gen();
    let rssi = rng.gen_range(-100, 20);
    let channel = generate_random_channel();
    let timestamp_nanos = 0;
    let snr_db = rng.gen_range(-20, 50);

    types::Bss {
        bssid,
        rssi,
        channel,
        timestamp_nanos,
        snr_db,
        observed_in_passive_scan: rng.gen::<bool>(),
        compatible: rng.gen::<bool>(),
        bss_description: random_fidl_bss_description!(
            bssid: bssid,
            rssi_dbm: rssi,
            channel: channel,
            timestamp: timestamp_nanos as u64,
            snr_db: snr_db,
        ),
    }
}

pub fn generate_random_scan_result() -> types::ScanResult {
    let mut rng = rand::thread_rng();
    let ssid = format!("scan result rand {}", rng.gen::<i32>()).as_bytes().to_vec();
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
