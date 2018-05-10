// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;
use rsna::SecAssocResult;

pub struct Supplicant {
    pub key_replay_counter: u64,
    pub a_nonce: [u8; 32],
}

impl Supplicant {
    pub fn new() -> Result<Supplicant, failure::Error> {
        Ok(Supplicant {
            key_replay_counter: 0,
            a_nonce: [0u8; 32],
        })
    }
}

impl Supplicant {
    pub fn on_eapol_key_frame(
        &self, _frame: &eapol::KeyFrame, _plain_data: &[u8],
    ) -> SecAssocResult {
        // TODO(hahnr): Implement.
        Ok(vec![])
    }
}
