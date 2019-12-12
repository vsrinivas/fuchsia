// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::rsn::Rsna,
    failure::{bail, Error},
    wlan_common::ie::write_wpa1_ie,
    wlan_rsn::ProtectionInfo,
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
                    bail!("found RSNE protection inside a WPA1 association...");
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
                    bail!("found WPA protection inside an RSNA...");
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
