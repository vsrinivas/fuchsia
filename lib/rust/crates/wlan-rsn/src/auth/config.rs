// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth::psk;
use Result;

#[derive(Debug)]
pub enum Config {
    Psk(psk::Config),
}

pub(crate) fn for_psk(passphrase: &[u8], ssid: &[u8]) -> Result<Config> {
    psk::Config::new(passphrase, ssid).map(|c| Config::Psk(c))
}