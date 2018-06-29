// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use super::{Error, Result};
use bytes::Bytes;
use std::fmt;
use suite_selector;

macro_rules! return_none_if_unknown_usage {
    ($e:expr) => {
        if !$e.has_known_usage() {
            return None;
        }
    };
}

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131
pub const GROUP_CIPHER_SUITE: u8 = 0;
pub const WEP_40: u8 = 1;
pub const TKIP: u8 = 2;
// 3 - Reserved.
pub const CCMP_128: u8 = 4;
pub const WEP_104: u8 = 5;
pub const BIP_CMAC_128: u8 = 6;
pub const GROUP_ADDRESSED_TRAFFIC_NOT_ALLOWED: u8 = 7;
pub const GCMP_128: u8 = 8;
pub const GCMP_256: u8 = 9;
pub const CCMP_256: u8 = 10;
pub const BIP_GMAC_128: u8 = 11;
pub const BIP_GMAC_256: u8 = 12;
pub const BIP_CMAC_256: u8 = 13;
// 14-255 - Reserved.

#[derive(PartialOrd, PartialEq, Clone)]
pub struct Cipher {
    pub oui: Bytes,
    pub suite_type: u8,
}

impl Cipher {
    /// Reserved and vendor specific cipher suites have no known usage and require special
    /// treatments.
    pub fn has_known_usage(&self) -> bool {
        !self.is_reserved() && !self.is_vendor_specific()
    }

    pub fn is_vendor_specific(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-131
        !&self.oui[..].eq(&suite_selector::OUI)
    }

    pub fn is_reserved(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-131
        (self.suite_type == 3 || self.suite_type >= 14) && !self.is_vendor_specific()
    }

    pub fn is_enhanced(&self) -> bool {
        // IEEE Std 802.11-2016, 4.3.8
        if !self.has_known_usage() {
            false
        } else {
            match self.suite_type {
                GROUP_CIPHER_SUITE | WEP_40 | WEP_104 | GROUP_ADDRESSED_TRAFFIC_NOT_ALLOWED => {
                    false
                }
                _ => true,
            }
        }
    }

    pub fn supports_gtk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            1...5 | 8...10 => Some(true),
            0 | 6 | 11...13 => Some(false),
            _ => None,
        }
    }

    pub fn supports_ptk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            0 | 2...4 | 8...10 => Some(true),
            1 | 5 | 6 | 11...13 => Some(false),
            _ => None,
        }
    }

    pub fn supports_igtk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            6 | 11...13 => Some(true),
            0 | 1...5 | 8...10 => Some(false),
            _ => None,
        }
    }

    pub fn tk_bits(&self) -> Option<u16> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 12.7.2, Table 12-4
        match self.suite_type {
            1 => Some(40),
            5 => Some(104),
            4 | 6 | 8 | 11 => Some(128),
            2 | 9 | 10 | 12 | 13 => Some(256),
            _ => None,
        }
    }
}

impl suite_selector::Factory for Cipher {
    type Suite = Cipher;

    fn new(oui: Bytes, suite_type: u8) -> Result<Self::Suite> {
        if oui.len() != 3 {
            Err(Error::InvalidOuiLength(oui.len()))
        } else {
            Ok(Cipher { oui, suite_type })
        }
    }
}

impl fmt::Debug for Cipher {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{:02X}-{:02X}-{:02X}:{}",
            self.oui[0], self.oui[1], self.oui[2], self.suite_type
        )
    }
}
