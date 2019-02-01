// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod psk;

use crate::rsna::{UpdateSink, VerifiedKeyFrame};
use failure;

#[derive(Debug, PartialEq)]
pub enum Method {
    Psk(psk::Psk),
}

impl Method {
    pub fn from_config(cfg: Config) -> Result<Method, failure::Error> {
        match cfg {
            Config::ComputedPsk(psk) => Ok(Method::Psk(psk)),
        }
    }

    // Unused as only PSK is supported so far.
    pub fn on_eapol_key_frame(
        &self,
        _update_sink: &mut UpdateSink,
        _frame: VerifiedKeyFrame,
    ) -> Result<(), failure::Error> {
        Ok(())
    }
}

#[derive(Debug, PartialEq)]
pub enum Config {
    ComputedPsk(psk::Psk),
}
