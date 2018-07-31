// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;
use rsna::{Role, SecAssocResult};

#[derive(Debug, Clone)]
pub struct Config {
    pub role: Role,
    pub sta_addr: [u8; 6],
    pub peer_addr: [u8; 6],
}

#[derive(Debug)]
pub struct GroupKey;

impl GroupKey {
    pub fn new(_cfg: Config, _ptk: Vec<u8>) -> Result<GroupKey, failure::Error> {
        // TODO(hahnr): Implement
        Ok(GroupKey)
    }

    pub fn on_eapol_key_frame(&mut self, _frame: &eapol::KeyFrame) -> SecAssocResult {
        // TODO(hahnr): Implement
        Ok(vec![])
    }
}