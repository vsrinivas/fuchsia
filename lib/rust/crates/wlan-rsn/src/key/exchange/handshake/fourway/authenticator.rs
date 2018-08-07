// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;
use rsna::SecAssocResult;

#[derive(Debug, PartialEq)]
pub struct Authenticator {
    pub key_replay_counter: u64,
    pub s_nonce: [u8; 32],
}

impl Authenticator {
    pub fn new() -> Result<Authenticator, failure::Error> {
        Ok(Authenticator {
            key_replay_counter: 0,
            s_nonce: [0u8; 32],
        })
    }

    pub fn initiate(&self) -> Result<(), failure::Error> {
        // TODO(hahnr): Send first message of handshake.
        Ok(())
    }

    pub fn on_eapol_key_frame(
        &self,
        _frame: &eapol::KeyFrame,
        _plain_data: &[u8],
    ) -> SecAssocResult {
        // TODO(hahnr): Implement.
        Ok(vec![])
    }
}
