// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::rsn::Rsna,
    anyhow::{format_err, Error},
    wep_deprecated,
    wlan_common::ie::write_wpa1_ie,
    wlan_rsn::{auth, ProtectionInfo},
};

#[derive(Debug)]
pub enum Protection {
    Open,
    Wep(wep_deprecated::Key),
    // WPA1 is based off of a modified pre-release version of IEEE 802.11i. It is similar enough
    // that we can reuse the existing RSNA implementation rather than duplicating large pieces of
    // logic.
    LegacyWpa(Rsna),
    Rsna(Rsna),
}

impl Protection {
    pub fn get_rsn_auth_method(&self) -> Option<auth::MethodName> {
        let rsna = match self {
            Self::LegacyWpa(rsna) => rsna,
            Self::Rsna(rsna) => rsna,
            // Neither WEP or Open use an RSN, so None is returned.
            Self::Wep(_) | Self::Open => {
                return None;
            }
        };

        Some(rsna.supplicant.get_auth_method())
    }
}

#[derive(Debug)]
pub enum ProtectionIe {
    Rsne(Vec<u8>),
    VendorIes(Vec<u8>),
}

/// Based on the type of protection, derive either RSNE or Vendor IEs:
/// No Protection or WEP: Neither
/// WPA2: RSNE
/// WPA1: Vendor IEs
pub(crate) fn build_protection_ie(protection: &Protection) -> Result<Option<ProtectionIe>, Error> {
    match protection {
        Protection::Open | Protection::Wep(_) => Ok(None),
        Protection::LegacyWpa(rsna) => {
            let s_protection = rsna.negotiated_protection.to_full_protection();
            let s_wpa = match s_protection {
                ProtectionInfo::Rsne(_) => {
                    return Err(format_err!("found RSNE protection inside a WPA1 association..."));
                }
                ProtectionInfo::LegacyWpa(wpa) => wpa,
            };
            let mut buf = vec![];
            // Writing an RSNE into a Vector can never fail as a Vector can be grown when more
            // space is required. If this panic ever triggers, something is clearly broken
            // somewhere else.
            write_wpa1_ie(&mut buf, &s_wpa).unwrap();
            Ok(Some(ProtectionIe::VendorIes(buf)))
        }
        Protection::Rsna(rsna) => {
            let s_protection = rsna.negotiated_protection.to_full_protection();
            let s_rsne = match s_protection {
                ProtectionInfo::Rsne(rsne) => rsne,
                ProtectionInfo::LegacyWpa(_) => {
                    return Err(format_err!("found WPA protection inside an RSNA..."));
                }
            };
            let mut buf = Vec::with_capacity(s_rsne.len());
            // Writing an RSNE into a Vector can never fail as a Vector can be grown when more
            // space is required. If this panic ever triggers, something is clearly broken
            // somewhere else.
            let () = s_rsne.write_into(&mut buf).unwrap();
            Ok(Some(ProtectionIe::Rsne(buf)))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::{
            rsn::Rsna,
            test_utils::{mock_psk_supplicant, mock_sae_supplicant},
        },
        wep_deprecated::derive_key,
        wlan_common::ie::{
            fake_ies::fake_wpa_ie,
            rsn::fake_rsnes::{fake_wpa2_s_rsne, fake_wpa3_s_rsne},
        },
        wlan_rsn::{rsna::NegotiatedProtection, ProtectionInfo},
    };

    #[test]
    fn test_get_rsn_auth_method() {
        // Open
        let protection = Protection::Open;
        assert!(protection.get_rsn_auth_method().is_none());

        // Wep
        let protection = Protection::Wep(derive_key(&[1; 5]).expect("unable to derive WEP key"));
        assert!(protection.get_rsn_auth_method().is_none());

        // WPA1
        let protection_info = ProtectionInfo::LegacyWpa(fake_wpa_ie());
        let negotiated_protection = NegotiatedProtection::from_protection(&protection_info)
            .expect("could create mocked WPA1 NegotiatedProtection");
        let protection = Protection::LegacyWpa(Rsna {
            negotiated_protection,
            supplicant: Box::new(mock_psk_supplicant().0),
        });
        assert_eq!(protection.get_rsn_auth_method(), Some(auth::MethodName::Psk));

        // WPA2
        let protection_info = ProtectionInfo::Rsne(fake_wpa2_s_rsne());
        let negotiated_protection = NegotiatedProtection::from_protection(&protection_info)
            .expect("could create mocked WPA2 NegotiatedProtection");
        let protection = Protection::Rsna(Rsna {
            negotiated_protection,
            supplicant: Box::new(mock_psk_supplicant().0),
        });
        assert_eq!(protection.get_rsn_auth_method(), Some(auth::MethodName::Psk));

        // WPA3
        let protection_info = ProtectionInfo::Rsne(fake_wpa3_s_rsne());
        let negotiated_protection = NegotiatedProtection::from_protection(&protection_info)
            .expect("could create mocked WPA3 NegotiatedProtection");
        let protection = Protection::Rsna(Rsna {
            negotiated_protection,
            supplicant: Box::new(mock_sae_supplicant().0),
        });
        assert_eq!(protection.get_rsn_auth_method(), Some(auth::MethodName::Sae));
    }
}
