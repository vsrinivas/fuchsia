// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;

pub struct Supplicant {
    // TODO(hahnr): Implement
}

impl Supplicant {
    pub fn new() -> Result<Supplicant, failure::Error> {
        Ok(Supplicant {})
    }
}

impl eapol::KeyFrameReceiver for Supplicant {
    fn on_eapol_key_frame(&self, _frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
        // TODO(hahnr): Implement.
        Ok(())
    }
}
