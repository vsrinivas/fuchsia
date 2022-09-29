// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    crate::client::types,
    fidl_fuchsia_wlan_common_security as fidl_security,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_zircon as zx,
    ieee80211::{Bssid, Ssid},
    rand::Rng as _,
    std::convert::TryFrom,
    wlan_common::{
        channel::{Cbw, Channel},
        random_fidl_bss_description,
        scan::Compatibility,
        security::SecurityDescriptor,
    },
};

pub fn generate_random_channel() -> Channel {
    let mut rng = rand::thread_rng();
    generate_channel(rng.gen::<u8>())
}

pub fn generate_channel(channel: u8) -> Channel {
    let mut rng = rand::thread_rng();
    Channel {
        primary: channel,
        cbw: match rng.gen_range(0..5) {
            0 => Cbw::Cbw20,
            1 => Cbw::Cbw40,
            2 => Cbw::Cbw40Below,
            3 => Cbw::Cbw80,
            4 => Cbw::Cbw160,
            5 => Cbw::Cbw80P80 { secondary80: rng.gen::<u8>() },
            _ => panic!(),
        },
    }
}

pub fn generate_random_sme_scan_result() -> fidl_sme::ScanResult {
    let mut rng = rand::thread_rng();
    fidl_sme::ScanResult {
        compatibility: match rng.gen_range(0..4) {
            0 => Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Open],
            })),
            1 => Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa2Personal],
            })),
            2 => Some(Box::new(fidl_sme::Compatibility {
                mutual_security_protocols: vec![
                    fidl_security::Protocol::Wpa2Personal,
                    fidl_security::Protocol::Wpa3Personal,
                ],
            })),
            _ => None,
        },
        timestamp_nanos: rng.gen(),
        bss_description: random_fidl_bss_description!(),
    }
}

pub fn generate_random_bss() -> types::Bss {
    let mut rng = rand::thread_rng();
    let bssid: Bssid = Bssid(rng.gen());
    let rssi = rng.gen_range(-100..20);
    let channel = generate_random_channel();
    let timestamp = zx::Time::from_nanos(rng.gen());
    let snr_db = rng.gen_range(-20..50);

    types::Bss {
        bssid,
        rssi,
        channel,
        timestamp,
        snr_db,
        observation: if rng.gen::<bool>() {
            types::ScanObservation::Passive
        } else {
            types::ScanObservation::Active
        },
        compatibility: match rng.gen_range(0..4) {
            0 => Compatibility::expect_some([SecurityDescriptor::OPEN]),
            1 => Compatibility::expect_some([SecurityDescriptor::WPA2_PERSONAL]),
            2 => Compatibility::expect_some([
                SecurityDescriptor::WPA2_PERSONAL,
                SecurityDescriptor::WPA3_PERSONAL,
            ]),
            _ => None,
        },
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
        compatibility: match rng.gen_range(0..2) {
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
        disconnect_source: match rng.gen_range(0..2) {
            0 => fidl_sme::DisconnectSource::Ap(generate_random_disconnect_cause()),
            1 => fidl_sme::DisconnectSource::User(generate_random_user_disconnect_reason()),
            2 => fidl_sme::DisconnectSource::Mlme(generate_random_disconnect_cause()),
            _ => panic!(),
        },
    }
}

pub fn generate_random_user_disconnect_reason() -> fidl_sme::UserDisconnectReason {
    let mut rng = rand::thread_rng();
    match rng.gen_range(0..14) {
        0 => fidl_sme::UserDisconnectReason::Unknown,
        1 => fidl_sme::UserDisconnectReason::FailedToConnect,
        2 => fidl_sme::UserDisconnectReason::FidlConnectRequest,
        3 => fidl_sme::UserDisconnectReason::FidlStopClientConnectionsRequest,
        4 => fidl_sme::UserDisconnectReason::ProactiveNetworkSwitch,
        5 => fidl_sme::UserDisconnectReason::DisconnectDetectedFromSme,
        6 => fidl_sme::UserDisconnectReason::RegulatoryRegionChange,
        7 => fidl_sme::UserDisconnectReason::Startup,
        8 => fidl_sme::UserDisconnectReason::NetworkUnsaved,
        9 => fidl_sme::UserDisconnectReason::NetworkConfigUpdated,
        10 => fidl_sme::UserDisconnectReason::WlanstackUnitTesting,
        11 => fidl_sme::UserDisconnectReason::WlanSmeUnitTesting,
        12 => fidl_sme::UserDisconnectReason::WlanServiceUtilTesting,
        13 => fidl_sme::UserDisconnectReason::WlanDevTool,
        _ => panic!(),
    }
}

pub fn generate_random_disconnect_cause() -> fidl_sme::DisconnectCause {
    fidl_sme::DisconnectCause {
        reason_code: generate_random_reason_code(),
        mlme_event_name: generate_random_disconnect_mlme_event_name(),
    }
}

pub fn generate_random_reason_code() -> fidl_ieee80211::ReasonCode {
    let mut rng = rand::thread_rng();
    // This is just a random subset from the first few reason codes
    match rng.gen_range(0..10) {
        0 => fidl_ieee80211::ReasonCode::UnspecifiedReason,
        1 => fidl_ieee80211::ReasonCode::InvalidAuthentication,
        2 => fidl_ieee80211::ReasonCode::LeavingNetworkDeauth,
        3 => fidl_ieee80211::ReasonCode::ReasonInactivity,
        4 => fidl_ieee80211::ReasonCode::NoMoreStas,
        5 => fidl_ieee80211::ReasonCode::InvalidClass2Frame,
        6 => fidl_ieee80211::ReasonCode::InvalidClass3Frame,
        7 => fidl_ieee80211::ReasonCode::LeavingNetworkDisassoc,
        8 => fidl_ieee80211::ReasonCode::NotAuthenticated,
        9 => fidl_ieee80211::ReasonCode::UnacceptablePowerCapability,
        _ => panic!(),
    }
}

pub fn generate_random_disconnect_mlme_event_name() -> fidl_sme::DisconnectMlmeEventName {
    let mut rng = rand::thread_rng();
    match rng.gen_range(0..2) {
        0 => fidl_sme::DisconnectMlmeEventName::DeauthenticateIndication,
        1 => fidl_sme::DisconnectMlmeEventName::DisassociateIndication,
        _ => panic!(),
    }
}
