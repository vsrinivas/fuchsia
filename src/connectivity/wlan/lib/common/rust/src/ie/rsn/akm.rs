// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::suite_selector;
use crate::ie::rsn::suite_selector::OUI;
use crate::organization::Oui;
use std::fmt;

macro_rules! return_none_if_unknown_algo {
    ($e:expr) => {
        if !$e.has_known_algorithm() {
            return None;
        }
    };
}

// IEEE Std 802.11-2016, 9.4.2.25.3, Table 9-133
// 0 - Reserved.
pub const EAP: u8 = 1;
pub const PSK: u8 = 2;
pub const FT_EAP: u8 = 3;
pub const FT_PSK: u8 = 4;
pub const EAP_SHA256: u8 = 5;
pub const PSK_SHA256: u8 = 6;
pub const TDLS: u8 = 7;
pub const SAE: u8 = 8;
pub const FT_SAE: u8 = 9;
pub const AP_PEERKEY: u8 = 10;
pub const EAP_SUITEB: u8 = 11;
pub const EAP_SUITEB_SHA384: u8 = 12;
pub const FT_EAP_SHA384: u8 = 13;
// 14-255 - Reserved.

// Shorthands for the most commonly constructed akm suites
pub const AKM_EAP: Akm = Akm::new_dot11(EAP);
pub const AKM_PSK: Akm = Akm::new_dot11(PSK);
pub const AKM_FT_PSK: Akm = Akm::new_dot11(FT_PSK);
pub const AKM_SAE: Akm = Akm::new_dot11(SAE);

#[derive(PartialOrd, PartialEq, Eq, Clone)]
pub struct Akm {
    pub oui: Oui,
    pub suite_type: u8,
}

impl Akm {
    /// Creates a new AKM instance for 802.11 specified AKMs.
    /// See IEEE Std 802.11-2016, 9.4.2.25.3, Table 9-133.
    pub const fn new_dot11(suite_type: u8) -> Self {
        Akm { oui: OUI, suite_type }
    }

    /// Only AKMs specified in IEEE 802.11-2016, 9.4.2.25.4, Table 9-133 have known algorithms.
    pub fn has_known_algorithm(&self) -> bool {
        if self.is_reserved() || self.is_vendor_specific() {
            // Support MSFT PSK for WPA1
            self.oui == Oui::MSFT && self.suite_type == PSK
        } else {
            self.suite_type != 7 && self.suite_type != 10
        }
    }

    pub fn is_vendor_specific(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.4, Table 9-133
        !self.oui.eq(&OUI)
    }

    pub fn is_reserved(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.4, Table 9-133
        (self.suite_type == 0 || self.suite_type >= 14) && !self.is_vendor_specific()
    }

    pub fn mic_bytes(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1..=11 => Some(16),
            12 | 13 => Some(24),
            _ => None,
        }
    }

    pub fn kck_bytes(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1..=11 => Some(16),
            12 | 13 => Some(24),
            _ => None,
        }
    }

    pub fn kek_bytes(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1..=11 => Some(16),
            12 | 13 => Some(32),
            _ => None,
        }
    }

    pub fn pmk_bytes(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.1.3
        match self.suite_type {
            1..=11 | 13 => Some(32),
            12 => Some(48),
            _ => None,
        }
    }

    #[deprecated(note = "use `kck_bytes` instead")]
    pub fn kck_bits(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1..=11 => Some(128),
            12 | 13 => Some(192),
            _ => None,
        }
    }

    #[deprecated(note = "use `kek_bytes` instead")]
    pub fn kek_bits(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1..=11 => Some(128),
            12 | 13 => Some(256),
            _ => None,
        }
    }

    #[deprecated(note = "use `pmk_bytes` instead")]
    pub fn pmk_bits(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.1.3
        match self.suite_type {
            1..=11 | 13 => Some(256),
            12 => Some(384),
            _ => None,
        }
    }
}

impl suite_selector::Factory for Akm {
    type Suite = Akm;

    fn new(oui: Oui, suite_type: u8) -> Self::Suite {
        Akm { oui, suite_type }
    }
}

impl fmt::Debug for Akm {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:02X}-{:02X}-{:02X}:{}", self.oui[0], self.oui[1], self.oui[2], self.suite_type)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_dot11() {
        let psk = AKM_PSK;
        assert!(!psk.is_vendor_specific());
        assert!(psk.has_known_algorithm());
        assert!(!psk.is_reserved());
    }

    #[test]
    fn test_msft_akm() {
        let psk = Akm { oui: Oui::MSFT, suite_type: PSK };
        assert!(psk.is_vendor_specific());
        assert!(psk.has_known_algorithm());
        assert!(!psk.is_reserved());
    }
}
