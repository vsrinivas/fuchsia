// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;

pub struct Authenticator {}

impl Authenticator {
    pub fn new() -> Result<Authenticator, failure::Error> {
        Ok(Authenticator {})
    }

    pub fn initiate(&self) -> Result<(), failure::Error> {
        // TODO(hahnr): Send first message of handshake.
        Ok(())
    }
}

impl eapol::KeyFrameReceiver for Authenticator {
    fn on_eapol_key_frame(&self, _frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
        // TODO(hahnr): Implement.
        Ok(())
    }
}
