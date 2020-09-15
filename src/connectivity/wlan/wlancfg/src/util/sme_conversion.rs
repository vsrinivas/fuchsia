// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme};

/// Match the protection type we receive from the SME in scan results to the Policy layer
/// security type.
pub fn security_from_sme_protection(
    protection: fidl_sme::Protection,
) -> Option<fidl_policy::SecurityType> {
    use fidl_policy::SecurityType;
    use fidl_sme::Protection::*;
    match protection {
        Wpa3Enterprise | Wpa3Personal | Wpa2Wpa3Personal => Some(SecurityType::Wpa3),
        Wpa2Enterprise | Wpa2Personal | Wpa1Wpa2Personal => Some(SecurityType::Wpa2),
        Wpa1 => Some(SecurityType::Wpa),
        Wep => Some(SecurityType::Wep),
        Open => Some(SecurityType::None),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn sme_protection_converts_to_policy_security() {
        use super::{
            fidl_policy::SecurityType, fidl_sme::Protection, security_from_sme_protection,
        };
        let test_pairs = vec![
            (Protection::Wpa3Enterprise, Some(SecurityType::Wpa3)),
            (Protection::Wpa3Personal, Some(SecurityType::Wpa3)),
            (Protection::Wpa2Wpa3Personal, Some(SecurityType::Wpa3)),
            (Protection::Wpa2Enterprise, Some(SecurityType::Wpa2)),
            (Protection::Wpa2Personal, Some(SecurityType::Wpa2)),
            (Protection::Wpa1Wpa2Personal, Some(SecurityType::Wpa2)),
            (Protection::Wpa1, Some(SecurityType::Wpa)),
            (Protection::Wep, Some(SecurityType::Wep)),
            (Protection::Open, Some(SecurityType::None)),
            (Protection::Unknown, None),
        ];
        for (input, output) in test_pairs {
            assert_eq!(security_from_sme_protection(input), output);
        }
    }
}
