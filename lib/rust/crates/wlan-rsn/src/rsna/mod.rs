// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;
use key::exchange::Key;

mod esssa;
#[cfg(test)]
pub mod test_util;

#[derive(Debug)]
pub enum Role {
    Authenticator,
    Supplicant,
}

pub enum SecAssocStatus {
    InvalidPassword,
}

pub enum SecAssocUpdate {
    TxEapolKeyFrame(eapol::KeyFrame),
    Key(Key),
    Status(SecAssocStatus),
}

pub type SecAssocResult = Result<Vec<SecAssocUpdate>, failure::Error>;
