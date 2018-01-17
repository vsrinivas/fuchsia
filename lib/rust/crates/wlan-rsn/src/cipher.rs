// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use std::fmt::{Debug, Formatter, Result};
use suite_selector;

macro_rules! return_none_if_unknown_usage {
    ($e:expr) => {
        if !$e.has_known_usage() {
            return None
        }
    };
}

pub struct Cipher {
    oui: [u8; 3],
    suite_type: u8,
}

impl Cipher {
    /// Reserved and vendor specific cipher suites have no known usage and require special
    /// treatments.
    fn has_known_usage(&self) -> bool {
        !self.is_reserved() && !self.is_vendor_specific()
    }

    pub fn is_vendor_specific(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-131
        !self.oui.eq(&suite_selector::OUI)
    }

    pub fn is_reserved(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-131
        (self.suite_type == 3 || self.suite_type >= 14) && !self.is_vendor_specific()
    }


    pub fn supports_gtk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            1 ... 5 | 8 ... 10 => Some(true),
            0 | 6 | 11 ... 13 => Some(false),
            _ => None,
        }
    }

    pub fn supports_ptk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            0 | 2 ... 4 | 8 ... 10 => Some(true),
            1 | 5 | 6 | 11 ... 13 => Some(false),
            _ => None,
        }
    }

    pub fn supports_igtk(&self) -> Option<bool> {
        return_none_if_unknown_usage!(self);

        // IEEE 802.11-2016, 9.4.2.25.2, Table 9-132
        match self.suite_type {
            6 | 11 ... 13 => Some(true),
            0 | 1 ... 5 | 8 ... 10 => Some(false),
            _ => None,
        }
    }
}

impl suite_selector::Factory for Cipher {
    type Suite = Cipher;

    fn new(oui: [u8; 3], suite_type: u8) -> Cipher {
        Cipher { oui, suite_type }
    }
}

impl Debug for Cipher {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{:02X}-{:02X}-{:02X}:{}", self.oui[0], self.oui[1], self.oui[2], self.suite_type)
    }
}