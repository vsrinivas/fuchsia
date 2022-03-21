// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use wlan_common::{
    ie::{
        rsn::{akm, cipher},
        wpa::WpaIe,
    },
    organization::Oui,
};

/// According to the WiFi Alliance WPA standard (2004), only TKIP support is required. We allow
/// CCMP if the AP requests it.
/// Only supported AKM is PSK
pub fn is_legacy_wpa_compatible(a_wpa: &WpaIe) -> bool {
    let multicast_supported = a_wpa.multicast_cipher.has_known_usage()
        && (a_wpa.multicast_cipher.suite_type == cipher::TKIP
            || a_wpa.multicast_cipher.suite_type == cipher::CCMP_128);
    let unicast_supported = a_wpa.unicast_cipher_list.iter().any(|c| {
        c.has_known_usage() && (c.suite_type == cipher::TKIP || c.suite_type == cipher::CCMP_128)
    });
    let akm_supported =
        a_wpa.akm_list.iter().any(|a| a.has_known_algorithm() && a.suite_type == akm::PSK);
    multicast_supported && unicast_supported && akm_supported
}

/// Construct a supplicant WPA1 IE with:
/// The same multicast and unicast ciphers as the AP WPA1 IE
/// PSK as the AKM
pub fn construct_s_wpa(a_wpa: &WpaIe) -> WpaIe {
    // Use CCMP if supported, otherwise default to TKIP.
    let unicast_cipher = if a_wpa
        .unicast_cipher_list
        .iter()
        .any(|c| c.has_known_usage() && c.suite_type == cipher::CCMP_128)
    {
        cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::CCMP_128 }
    } else {
        cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }
    };
    WpaIe {
        multicast_cipher: a_wpa.multicast_cipher.clone(),
        unicast_cipher_list: vec![unicast_cipher],
        akm_list: vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils;

    #[test]
    fn test_incompatible_multicast_cipher() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.multicast_cipher = cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::WEP_40 };
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_incompatible_unicast_cipher() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.unicast_cipher_list =
            vec![cipher::Cipher { oui: Oui::DOT11, suite_type: cipher::WEP_40 }];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_incompatible_akm() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.akm_list = vec![akm::Akm { oui: Oui::DOT11, suite_type: akm::EAP }];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn get_supplicant_ie_tkip() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.unicast_cipher_list =
            vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }];
        let s_wpa = construct_s_wpa(&a_wpa);
        assert_eq!(
            s_wpa.unicast_cipher_list,
            vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }]
        );
    }

    #[test]
    fn get_supplicant_ie_ccmp() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.unicast_cipher_list = vec![
            cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP },
            cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::CCMP_128 },
        ];
        let s_wpa = construct_s_wpa(&a_wpa);
        assert_eq!(
            s_wpa.unicast_cipher_list,
            vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::CCMP_128 }]
        );
    }

    #[test]
    fn test_no_unicast_cipher() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.unicast_cipher_list = vec![];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_no_akm() {
        let mut a_wpa = test_utils::make_wpa1_ie();
        a_wpa.akm_list = vec![];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }
}
