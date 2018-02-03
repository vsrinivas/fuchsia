// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth::psk;
use Result;

#[derive(Debug)]
pub enum Config {
    Psk(psk::Config),
}

pub fn for_psk(ssid: &[u8], passphrase: &[u8]) -> Result<Config> {
    psk::Config::new(ssid, passphrase).map(|c| Config::Psk(c))
}