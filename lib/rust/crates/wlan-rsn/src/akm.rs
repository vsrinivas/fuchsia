// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use auth::{config, psk};
use futures::Future;
use integrity;
use keywrap;
use std::fmt;
use suite_selector;
use super::{Error, Result};

macro_rules! return_none_if_unknown_algo {
    ($e:expr) => {
        if !$e.has_known_algorithm() {
            return None
        }
    };
}

pub struct Akm<'a> {
    pub oui: &'a [u8],
    pub suite_type: u8,
}

impl<'a> Akm<'a> {
    /// Only AKMs specified in IEEE 802.11-2016, 9.4.2.25.4, Table 9-133 have known algorithms.
    fn has_known_algorithm(&self) -> bool {
        if self.is_reserved() || self.is_vendor_specific() {
            false
        } else {
            self.suite_type != 7 && self.suite_type != 10
        }
    }

    pub fn is_vendor_specific(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.4, Table 9-133
        !self.oui.eq(&suite_selector::OUI)
    }

    pub fn is_reserved(&self) -> bool {
        // IEEE 802.11-2016, 9.4.2.25.4, Table 9-133
        (self.suite_type == 0 || self.suite_type >= 14) && !self.is_vendor_specific()
    }

    pub fn mic_bytes(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1 ... 11 => Some(16),
            12 | 13 => Some(24),
            _ => None,
        }
    }

    pub fn kck_bits(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1 ... 11 => Some(128),
            12 | 13 => Some(192),
            _ => None,
        }
    }

    pub fn kek_bits(&self) -> Option<u16> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1 ... 11 => Some(128),
            12 | 13 => Some(256),
            _ => None,
        }
    }

    pub fn integrity_algorithm(&self) -> Option<Box<integrity::Algorithm>> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1 | 2 => Some(Box::new(integrity::HmacSha1128)),
            // TODO(hahnr): Add remaining integrity algorithms.
            3 ... 13 => None,
            _ => None
        }
    }

    pub fn keywrap_algorithm(&self) -> Option<Box<keywrap::Algorithm>> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 12.7.3, Table 12-8
        match self.suite_type {
            1 ... 13 => Some(Box::new(keywrap::NistAes)),
            _ => None
        }
    }

    /// Returns `None` if there is no known authentication method associated with this AKM.
    /// Else, returns a `Result` which indicates an error if the given `config` is incompatible with
    /// the AKM, or succeeds if the authentication method was successfully created.
    pub fn auth_method(&self, config: config::Config)
                       -> Option<Result<Box<Future<Item=Vec<u8>, Error=Error>>>> {
        return_none_if_unknown_algo!(self);

        // IEEE 802.11-2016, 9.4.2.25.3, Table 9-133
        match self.suite_type {
            2 => Some(match config {
                // TODO(hahnr): Due to a compiler bug we cannot use `.map` on the newly created PSK,
                // and instead must manually map the result to a boxed Future.
                config::Config::Psk(c) => match psk::new(c) {
                    Ok(psk) => Ok(Box::new(psk)),
                    Err(e) => Err(e),
                },
                _ => Err(Error::IncompatibleConfig(config, "PSK".to_string()))
            }),
            _ => None
        }
    }
}

impl<'a> suite_selector::Factory<'a> for Akm<'a> {
    type Suite = Akm<'a>;

    fn new(oui: &'a [u8], suite_type: u8) -> Result<Self::Suite> {
        if oui.len() != 3 {
            Err(Error::InvalidOuiLength(oui.len()))
        } else {
            Ok(Akm { oui, suite_type })
        }
    }
}

impl<'a> fmt::Debug for Akm<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{:02X}-{:02X}-{:02X}:{}", self.oui[0], self.oui[1], self.oui[2], self.suite_type)
    }
}