// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures;
use super::{Error, Result};

#[derive(Debug)]
pub struct Psk {
    config: Config,
}

pub fn new(config: Config) -> Result<Psk> {
    Ok(Psk { config })
}

#[derive(Debug)]
pub struct Config {
    ssid: Vec<u8>,
    passphrase: Vec<u8>,
}

impl Config {
    pub fn new(ssid: &[u8], passphrase: &[u8]) -> Result<Config> {
        // IEEE Std 802.11-2016, 9.4.2.2
        if ssid.len() > 32 {
            return Err(Error::InvalidSsidLen(ssid.len()));
        }

        // IEEE Std 802.11-2016, J.4.1
        if passphrase.len() < 8 || passphrase.len() > 63 {
            Err(Error::InvalidPassphraseLen(passphrase.len()))
        } else {
            for c in passphrase {
                if *c < 32 || *c > 126 {
                    return Err(Error::InvalidPassphraseChar(*c));
                }
            }
            Ok(Config { ssid: ssid.to_vec(), passphrase: passphrase.to_vec() })
        }
    }
}

impl futures::Future for Psk {
    type Item = Vec<u8>;
    type Error = Error;

    fn poll(&mut self) -> futures::Poll<Self::Item, Self::Error> {
        // TODO(hahnr): Use PBKDF2 to derive key (IEEE Std 802.11-2016, J.4.1)
        unimplemented!()
    }
}