// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use wlan_rsn::{akm, cipher, rsne::{RsnCapabilities, Rsne}, suite_selector::OUI};

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