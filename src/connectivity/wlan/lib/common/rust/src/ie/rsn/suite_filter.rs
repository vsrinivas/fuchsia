// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        akm::{self, Akm, AKM_EAP},
        cipher::{self, Cipher, CIPHER_BIP_CMAC_128, CIPHER_CCMP_128},
    },
    crate::ie,
};

// IEEE 802.11-2016, 9.4.2.25.2
// If group data cipher suite field is not included in RSNE, CCMP-128 is the default for
// non-DMG STA.
pub const DEFAULT_GROUP_DATA_CIPHER: Cipher = CIPHER_CCMP_128;

// IEEE 802.11-2016, 9.4.2.25.2
// If pairwise cipher suite field is not included in RSNE, CCMP-128 is the default for
// non-DMG STA.
pub const DEFAULT_PAIRWISE_CIPHER: [Cipher; 1] = [CIPHER_CCMP_128];

// IEEE 802.11-2016, 9.4.2.25.3
// If akm suite list field is not included in RSNE, suite selector value 00-0F-AC:1
// (i.e. akm::EAP) is the default.
pub const DEFAULT_AKM: [Akm; 1] = [AKM_EAP];

// IEEE 802.11-2016, 9.4.2.25.2
// If management frame protection is enabled, and group management cipher suite field is
// not included in RSNE, BIP-CMAC-128 is the default.
pub const DEFAULT_GROUP_MGMT_CIPHER: Cipher = CIPHER_BIP_CMAC_128;

pub struct SuiteFilter<'a> {
    known_group_data_ciphers: &'a [u8],
    known_akms: &'a [u8],
    known_pairwise_ciphers: &'a [u8],
    required_group_mgmt_cipher: Option<u8>,
}

impl<'a> SuiteFilter<'a> {
    pub fn is_satisfied(&self, rsne: &ie::rsn::rsne::Rsne) -> bool {
        let group_data_cipher =
            rsne.group_data_cipher_suite.as_ref().unwrap_or(&DEFAULT_GROUP_DATA_CIPHER);
        let group_data_satisfied = group_data_cipher.has_known_usage()
            && self.known_group_data_ciphers.iter().any(|s| group_data_cipher.suite_type == *s);

        let akms = if rsne.akm_suites.is_empty() { &DEFAULT_AKM[..] } else { &rsne.akm_suites[..] };
        let akm_satisfied = akms
            .iter()
            .any(|a| a.has_known_algorithm() && self.known_akms.iter().any(|s| a.suite_type == *s));

        let pairwise_ciphers = if rsne.pairwise_cipher_suites.is_empty() {
            &DEFAULT_PAIRWISE_CIPHER[..]
        } else {
            &rsne.pairwise_cipher_suites[..]
        };
        let pairwise_satisfied = pairwise_ciphers.iter().any(|c| {
            c.has_known_usage() && self.known_pairwise_ciphers.iter().any(|s| c.suite_type == *s)
        });

        let group_mgmt_cipher =
            rsne.group_mgmt_cipher_suite.as_ref().unwrap_or(&DEFAULT_GROUP_MGMT_CIPHER);
        let group_mgmt_satisfied = self
            .required_group_mgmt_cipher
            .map(|s| group_mgmt_cipher.suite_type == s)
            .unwrap_or(true);

        group_data_satisfied && akm_satisfied && pairwise_satisfied && group_mgmt_satisfied
    }
}

/// WFA, WPA1 Spec. 3.1, Chapter 2.1
pub const WPA1_PERSONAL: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::TKIP, cipher::CCMP_128],
    known_akms: &[akm::PSK],
    known_pairwise_ciphers: &[cipher::TKIP, cipher::CCMP_128],
    required_group_mgmt_cipher: None,
};

pub const WPA2_LEGACY: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::TKIP],
    known_akms: &[akm::PSK],
    known_pairwise_ciphers: &[cipher::TKIP],
    required_group_mgmt_cipher: None,
};

/// IEEE 802.11 2004, Chapter 7.3.2.25.1
///
/// Ciphers that encompass different configurations used for WPA2 personal (e.g. base
/// configuration, configuration that supports BSS fast transition, and configuration
/// that supports management frame protection)
pub const WPA2_PERSONAL: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::CCMP_128, cipher::TKIP],
    // From observation: In most WPA2 cases, only akm::PSK is included. If FT is enabled,
    // akm::FT_PSK is also included.
    // In the case where management frame protection is set to required, akm::PSK is replaced
    // with akm::PSK_SHA256 (but not when mfp is only set to capable)
    known_akms: &[akm::PSK, akm::FT_PSK, akm::PSK_SHA256],
    // In theory, 256 bit cipher suites (which were added in 802.11ac amendment) could also
    // be included here and elsewhere. In practice, it's unclear how many APs actually support
    // it. We'll disallow it for now to keep logic simple because otherwise, we also have
    // to do the check that pairwise and group keys match.
    //
    // TODO(fxbug.dev/29878): deploy metric to measure AKM and cipher use in practice.
    known_pairwise_ciphers: &[cipher::CCMP_128],
    required_group_mgmt_cipher: None,
};

/// WFA, WPA3 Spec. 1.0, Chapter 3
pub const WPA3_PERSONAL: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::CCMP_128],
    // WPA3 spec doesn't mention Fast BSS Transition, thus akm::FT_SAE is likely not supported.
    // For some reason, An AP we use for testing provide an option to turn on FT for WPA3 network,
    // but even then it still uses akm::SAE.
    known_akms: &[akm::SAE],
    known_pairwise_ciphers: &[cipher::CCMP_128],
    required_group_mgmt_cipher: None,
};

/// Ciphers that encompass different configurations used for WPA2 enterprise (e.g. base
/// configuration, configuration that supports BSS fast transition, and configuration
/// that supports management frame protection)
pub const WPA2_ENTERPRISE: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::CCMP_128],
    // From observation: akm::EAP is included for base WPA2 enterprise configuration. If FT
    // is enabled, akm::FT_EAP is also included.
    // In the case where management frame protection is set to required, akm::EAP is replaced
    // with akm::EAP_SHA256 (but not when mfp is only set to capable).
    known_akms: &[akm::EAP, akm::FT_EAP, akm::EAP_SHA256],
    known_pairwise_ciphers: &[cipher::CCMP_128],
    required_group_mgmt_cipher: None,
};

/// WFA, WPA3 Spec. 1.0, Chapter 3
pub const WPA3_ENTERPRISE_192_BIT: SuiteFilter = SuiteFilter {
    known_group_data_ciphers: &[cipher::GCMP_256],
    known_akms: &[akm::EAP_SUITEB_SHA384],
    known_pairwise_ciphers: &[cipher::GCMP_256],
    required_group_mgmt_cipher: Some(cipher::BIP_GMAC_256),
};

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::fake_frames::{fake_wpa2_rsne, fake_wpa3_enterprise_192_bit_rsne},
    };

    #[test]
    fn test_suite_filter() {
        let wpa2_rsne = ie::rsn::rsne::from_bytes(&fake_wpa2_rsne()[..]).unwrap().1;
        assert!(WPA2_PERSONAL.is_satisfied(&wpa2_rsne));
        assert!(!WPA3_PERSONAL.is_satisfied(&wpa2_rsne));
    }

    #[test]
    fn test_suite_filter_with_required_group_mgmt() {
        let mut wpa3_ent_rsne =
            ie::rsn::rsne::from_bytes(&fake_wpa3_enterprise_192_bit_rsne()[..]).unwrap().1;
        assert!(WPA3_ENTERPRISE_192_BIT.is_satisfied(&wpa3_ent_rsne));
        wpa3_ent_rsne.group_mgmt_cipher_suite = None;
        assert!(!WPA3_ENTERPRISE_192_BIT.is_satisfied(&wpa3_ent_rsne));
    }

    #[test]
    fn test_suite_filter_empty_rsne() {
        let rsne = ie::rsn::rsne::Rsne::default();
        assert!(WPA2_ENTERPRISE.is_satisfied(&rsne));
        assert!(!WPA2_PERSONAL.is_satisfied(&rsne));
    }
}
