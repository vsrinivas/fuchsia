// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use wlan_rsn::{
    akm::{self, Akm},
    cipher::{self, Cipher},
    key::{ptk::Ptk, gtk::Gtk},
    rsne::{RsnCapabilities, Rsne},
    suite_selector::OUI
};

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

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: Bytes::from(&OUI[..]), suite_type }
}

pub fn eapol_key_frame_bytes() -> Vec<u8> {
    // Content doesn't matter; we just need a valid EAPOL key frame to test our code path
    vec![
        0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79,
        0xfe, 0xc3, 0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25,
        0xf8, 0xc7, 0xca, 0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x01, 0x02, 0x03,
    ]
}

pub fn eapol_key_frame() -> eapol::KeyFrame {
    eapol::key_frame_from_bytes(&eapol_key_frame_bytes(), 16)
        .to_full_result()
        .expect("expect valid eapol key frame")
}

pub fn ptk() -> Ptk {
    let mut ptk_bytes = vec![];
    // Using different values for KCK, KEK,, and TK to detect potential mistakes. This ensures
    // that if our code, for example, mistakenly uses KCK instead of TK, test would fail.
    ptk_bytes.extend(vec![0xAAu8; akm().kck_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xBBu8; akm().kek_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xCCu8; cipher().tk_bytes().unwrap()]);
    Ptk::from_ptk(ptk_bytes, &akm(), cipher()).expect("expect valid ptk")
}

pub fn gtk_bytes() -> Vec<u8> {
    vec![0xDD; 16]
}

pub fn gtk() -> Gtk {
    Gtk::from_gtk(gtk_bytes(), 2, cipher()).expect("failed creating GTK")
}

pub fn akm() -> Akm {
    Akm {
        oui: Bytes::from(&OUI[..]),
        suite_type: akm::PSK,
    }
}

pub fn cipher() -> Cipher {
    Cipher {
        oui: Bytes::from(&OUI[..]),
        suite_type: cipher::CCMP_128,
    }
}
