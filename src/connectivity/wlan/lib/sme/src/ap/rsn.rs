// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use wlan_common::ie::rsn::{
    akm, cipher,
    rsne::{RsnCapabilities, Rsne},
    OUI,
};
use wlan_rsn::NegotiatedProtection;

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: OUI, suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: OUI, suite_type }
}

fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = data.map(make_cipher);
    rsne.pairwise_cipher_suites = pairwise.into_iter().map(make_cipher).collect();
    rsne.akm_suites = akms.into_iter().map(make_akm).collect();
    rsne.rsn_capabilities = Some(RsnCapabilities(0));
    rsne
}

/// Verify that Supplicant RSNE is a valid NegotiatedProtection, and is a subset of Authenticator
/// RSNE
pub fn is_valid_rsne_subset(s_rsne: &Rsne, a_rsne: &Rsne) -> Result<bool, anyhow::Error> {
    let s_caps = s_rsne.rsn_capabilities.as_ref().unwrap_or(&RsnCapabilities(0));
    let s_mgmt_req = s_caps.mgmt_frame_protection_req();
    let s_mgmt_cap = s_caps.mgmt_frame_protection_cap();
    let a_caps = a_rsne.rsn_capabilities.as_ref().unwrap_or(&RsnCapabilities(0));
    let a_mgmt_req = a_caps.mgmt_frame_protection_req();
    let a_mgmt_cap = a_caps.mgmt_frame_protection_cap();

    // IEEE Std 802.11-2016, 12.6.3, Table 12-2
    match (a_mgmt_cap, a_mgmt_req, s_mgmt_cap, s_mgmt_req) {
        (true, _, false, true) => {
            return Err(format_err!("supplicant RSNE has invalid mgmt frame protection"))
        }
        (false, true, true, _) => {
            return Err(format_err!("authenticator RSNE has invalid mgmt frame protection"))
        }
        (true, true, false, false) => return Ok(false),
        (false, false, true, true) => return Ok(false),
        // the remaining cases fall into either of these buckets:
        // 1 - spec mentions that "The AP may associate with the STA"
        // 2 - it's not covered in the spec, which means presumably we can ignore it. For example,
        //     if AP/client is not management frame protection capable, then it probably doesn't
        //     matter whether the opposite party advertises an invalid setting
        _ => (),
    }

    let neg_rsne = NegotiatedProtection::from_rsne(&s_rsne)?;

    Ok(a_rsne.group_data_cipher_suite.iter().any(|c| *c == neg_rsne.group_data)
        && a_rsne.pairwise_cipher_suites.iter().any(|c| *c == neg_rsne.pairwise)
        && a_rsne.akm_suites.iter().any(|c| *c == neg_rsne.akm))
}

pub fn create_wpa2_psk_rsne() -> Rsne {
    // Note: TKIP is legacy and considered insecure. Only allow CCMP usage for group and pairwise
    // ciphers.
    make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_wpa2_psk_rsne_bytes() {
        // Compliant with IEEE Std 802.11-2016, 9.4.2.25.
        let expected: Vec<u8> = vec![
            0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
        ];
        let rsne = create_wpa2_psk_rsne();
        let mut actual = Vec::with_capacity(rsne.len());
        rsne.write_into(&mut actual).expect("error writing RSNE");

        assert_eq!(&expected[..], &actual[..]);
    }

    #[test]
    fn test_valid_rsne() {
        let s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert!(is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }

    #[test]
    fn test_supplicant_rsne_has_too_many_suites() {
        let s_rsne =
            make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP, akm::PSK]);
        let a_rsne =
            make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP, akm::PSK]);
        let result = is_valid_rsne_subset(&s_rsne, &a_rsne);
        assert!(result.is_err());
        assert!(format!("{:?}", result.unwrap_err()).contains("negotiated protection is invalid"));
    }

    #[test]
    fn test_supplicant_missing_required_mpfc() {
        let mut s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        s_rsne.rsn_capabilities = Some(RsnCapabilities(0));
        let mut a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_req(true);
        rsn_cap.set_mgmt_frame_protection_cap(true);
        a_rsne.rsn_capabilities = Some(rsn_cap);
        assert!(!is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }

    #[test]
    fn test_authenticator_missing_required_mpfc() {
        let mut s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_req(true);
        rsn_cap.set_mgmt_frame_protection_cap(true);
        s_rsne.rsn_capabilities = Some(rsn_cap);
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert!(!is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }

    #[test]
    fn test_supplicant_has_invalid_mgmt_frame_protection_fields() {
        let mut s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_req(true);
        rsn_cap.set_mgmt_frame_protection_cap(false);
        s_rsne.rsn_capabilities = Some(rsn_cap);

        // AP only cares about client's invalid setting if AP is mgmt frame protection capable
        let mut a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_cap(true);
        a_rsne.rsn_capabilities = Some(rsn_cap);

        let result = is_valid_rsne_subset(&s_rsne, &a_rsne);
        assert!(result.is_err());
        assert_eq!(
            format!("{}", result.unwrap_err()),
            "supplicant RSNE has invalid mgmt frame protection"
        )
    }

    #[test]
    fn test_authenticator_has_invalid_mgmt_frame_protection_fields() {
        // client only cares about AP's invalid setting if client is mgmt frame protection capable
        let mut s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_cap(true);
        s_rsne.rsn_capabilities = Some(rsn_cap);

        let mut a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let mut rsn_cap = RsnCapabilities(0);
        rsn_cap.set_mgmt_frame_protection_req(true);
        rsn_cap.set_mgmt_frame_protection_cap(false);
        a_rsne.rsn_capabilities = Some(rsn_cap);

        let result = is_valid_rsne_subset(&s_rsne, &a_rsne);
        assert!(result.is_err());
        assert_eq!(
            format!("{}", result.unwrap_err()),
            "authenticator RSNE has invalid mgmt frame protection"
        )
    }

    #[test]
    fn test_rsne_unsupported_group_data_cipher() {
        let s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let a_rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        assert!(!is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }

    #[test]
    fn test_rsne_unsupported_pairwise_cipher() {
        let s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::BIP_CMAC_256], vec![akm::PSK]);
        assert!(!is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }

    #[test]
    fn test_rsne_unsupported_akm() {
        let s_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP]);
        assert!(!is_valid_rsne_subset(&s_rsne, &a_rsne).expect("expect Ok result"));
    }
}
