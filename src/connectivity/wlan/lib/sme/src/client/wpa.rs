// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use fidl_fuchsia_wlan_mlme::BssDescription;

use fidl_fuchsia_wlan_sme as fidl_sme;
use wlan_common::bss::BssDescriptionExt;
use wlan_common::ie::rsn::{akm, cipher};
use wlan_common::ie::wpa::WpaIe;
use wlan_common::organization::Oui;
use wlan_rsn::{self, nonce::NonceReader, NegotiatedProtection, ProtectionInfo};

use super::rsn::{compute_psk, Rsna};
use crate::{client::protection::Protection, DeviceInfo};

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

/// Builds a supplicant for establishing a WPA1 association. WPA Assocation is not an official term,
/// but is used here to refer to the modified RSNA used by WPA1.
pub fn get_legacy_wpa_association(
    device_info: &DeviceInfo,
    credential: &fidl_sme::Credential,
    bss: &BssDescription,
) -> Result<Protection, anyhow::Error> {
    let a_wpa = bss.get_wpa_ie()?;
    if !is_legacy_wpa_compatible(&a_wpa) {
        return Err(format_err!("incompatible legacy WPA {:?}", a_wpa));
    }
    let s_wpa = construct_s_wpa(&a_wpa);
    let negotiated_protection = NegotiatedProtection::from_legacy_wpa(&s_wpa)?;
    let psk = compute_psk(credential, &bss.ssid[..])?;
    let supplicant = wlan_rsn::Supplicant::new_wpa_personal(
        // Note: There should be one Reader per device, not per SME.
        // Follow-up with improving on this.
        NonceReader::new(&device_info.addr[..])?,
        psk,
        device_info.addr,
        ProtectionInfo::LegacyWpa(s_wpa),
        bss.bssid,
        ProtectionInfo::LegacyWpa(a_wpa),
    )
    .map_err(|e| format_err!("failed to create ESS-SA: {:?}", e))?;
    Ok(Protection::LegacyWpa(Rsna { negotiated_protection, supplicant: Box::new(supplicant) }))
}

/// Construct a supplicant WPA1 IE with:
/// The same multicast and unicast ciphers as the AP WPA1 IE
/// PSK as the AKM
fn construct_s_wpa(a_wpa: &WpaIe) -> WpaIe {
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
    use crate::{
        client::test_utils::{
            fake_protected_bss_description, fake_unprotected_bss_description,
            fake_wpa1_bss_description,
        },
        test_utils::{fake_device_info, make_wpa1_ie},
    };

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];

    #[test]
    fn test_incompatible_multicast_cipher() {
        let mut a_wpa = make_wpa1_ie();
        a_wpa.multicast_cipher = cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::WEP_40 };
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_incompatible_unicast_cipher() {
        let mut a_wpa = make_wpa1_ie();
        a_wpa.unicast_cipher_list =
            vec![cipher::Cipher { oui: Oui::DOT11, suite_type: cipher::WEP_40 }];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_incompatible_akm() {
        let mut a_wpa = make_wpa1_ie();
        a_wpa.akm_list = vec![akm::Akm { oui: Oui::DOT11, suite_type: akm::EAP }];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn get_supplicant_ie_tkip() {
        let mut a_wpa = make_wpa1_ie();
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
        let mut a_wpa = make_wpa1_ie();
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
        let mut a_wpa = make_wpa1_ie();
        a_wpa.unicast_cipher_list = vec![];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_no_akm() {
        let mut a_wpa = make_wpa1_ie();
        a_wpa.akm_list = vec![];
        assert_eq!(is_legacy_wpa_compatible(&a_wpa), false);
    }

    #[test]
    fn test_get_wpa_password_for_unprotected_network() {
        let bss = fake_unprotected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Password("somepass".as_bytes().to_vec());
        get_legacy_wpa_association(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expect error when password is supplied for unprotected network");
    }

    #[test]
    fn test_get_wpa_no_password_for_protected_network() {
        let bss = fake_wpa1_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        get_legacy_wpa_association(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expect error when no password is supplied for protected network");
    }

    #[test]
    fn test_get_wpa_for_rsna_protected_network() {
        let bss = fake_protected_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        get_legacy_wpa_association(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expect error when treating RSNA as WPA association");
    }

    #[test]
    fn test_get_wpa_psk() {
        let bss = fake_wpa1_bss_description(b"foo_bss".to_vec());
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 32]);
        get_legacy_wpa_association(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect("expected successful RSNA with valid PSK");
    }

    #[test]
    fn test_get_wpa_invalid_psk() {
        let bss = fake_wpa1_bss_description(b"foo_bss".to_vec());
        // PSK too short
        let credential = fidl_sme::Credential::Psk(vec![0xAA; 31]);
        get_legacy_wpa_association(&fake_device_info(CLIENT_ADDR), &credential, &bss)
            .expect_err("expected RSNA failure with invalid PSK");
    }
}
