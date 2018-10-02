// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use wlan_rsn::{akm, cipher, rsne::{RsnCapabilities, Rsne}, suite_selector::OUI};

use crate::{InfoEvent, InfoStream, Ssid};

fn fake_bss_description(ssid: Ssid, rsn: Option<Vec<u8>>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [0, 0, 0, 0, 0, 0],
        ssid,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        cap: fidl_mlme::CapabilityInfo {
            ess: false,
            ibss: false,
            cf_pollable: false,
            cf_poll_req: false,
            privacy: false,
            short_preamble: false,
            spectrum_mgmt: false,
            qos: false,
            short_slot_time: false,
            apsd: false,
            radio_msmt: false,
            delayed_block_ack: false,
            immediate_block_ack: false,
        },
        country: None,
        rsn,

        rcpi_dbmh: 0,
        rsni_dbh: 0,

        ht_cap: None,
        ht_op: None,
        vht_cap: None,
        vht_op: None,
        chan: fidl_mlme::WlanChan {
            primary: 1,
            secondary80: 0,
            cbw: fidl_mlme::Cbw::Cbw20,
        },
        rssi_dbm: 0,
    }
}

pub fn fake_bss_with_bssid(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid,
        .. fake_unprotected_bss_description(ssid)
    }
}

pub fn fake_unprotected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fake_bss_description(ssid, None)
}

pub fn fake_protected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
    fake_bss_description(ssid, Some(rsne_as_bytes(a_rsne)))
}

pub fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
    let a_rsne = Rsne {
        version: 1,
        group_data_cipher_suite: data.map(|t| make_cipher(t)),
        pairwise_cipher_suites: pairwise.into_iter().map(|t| make_cipher(t)).collect(),
        akm_suites: akms.into_iter().map(|t| make_akm(t)).collect(),
        ..Default::default()
    };
    a_rsne
}

pub fn wpa2_psk_ccmp_rsne_with_caps(caps: RsnCapabilities) -> Rsne {
    let a_rsne = Rsne {
        version: 1,
        group_data_cipher_suite: Some(make_cipher(cipher::CCMP_128)),
        pairwise_cipher_suites: vec![make_cipher(cipher::CCMP_128)],
        akm_suites: vec![make_akm(akm::PSK)],
        rsn_capabilities: Some(caps),
        ..Default::default()
    };
    a_rsne
}

pub fn rsne_as_bytes(s_rsne: Rsne) -> Vec<u8> {
    let mut buf = Vec::with_capacity(s_rsne.len());
    s_rsne.as_bytes(&mut buf);
    buf
}

pub fn expect_info_event(info_stream: &mut InfoStream, expected_event: InfoEvent) {
    if let Ok(Some(e)) = info_stream.try_next() {
        assert_eq!(e, expected_event);
    } else {
        panic!("expect event to InfoSink");
    }
}

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
}
