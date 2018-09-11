// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use wlan_rsn::{akm, cipher, rsne::{Rsne, RsnCapabilities}, suite_selector::OUI};

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: Bytes::from(&OUI[..]), suite_type }
}

fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = data.map(make_cipher);
    rsne.pairwise_cipher_suites = pairwise.into_iter().map(make_cipher).collect();
    rsne.akm_suites = akms.into_iter().map(make_akm).collect();
    rsne.rsn_capabilities = Some(RsnCapabilities(0));
    rsne
}

pub fn create_wpa2_psk_rsne() -> Vec<u8> {
    // Note: TKIP is legacy and considered insecure. Only allow CCMP usage for group and pairwise
    // ciphers.
    let rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
    let mut buf = Vec::with_capacity(rsne.len());
    rsne.as_bytes(&mut buf);
    buf
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_as_bytes() {
        // Compliant with IEEE Std 802.11-2016, 9.4.2.25.
        let expected: Vec<u8> = vec![
            0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
        ];
        let actual = create_wpa2_psk_rsne();
        assert_eq!(&expected[..], &actual[..]);
    }
}