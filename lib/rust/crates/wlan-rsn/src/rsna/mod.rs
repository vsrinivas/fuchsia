// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use failure;
use key::exchange::Key;

pub mod esssa;
#[cfg(test)]
pub mod test_util;

#[derive(Debug, Clone)]
pub enum Role {
    Authenticator,
    Supplicant,
}

#[derive(Debug)]
pub enum SecAssocStatus {
    WrongPassword,
    EssSaEstablished,
}

#[derive(Debug)]
pub enum SecAssocUpdate {
    TxEapolKeyFrame(eapol::KeyFrame),
    Key(Key),
    Status(SecAssocStatus),
}

pub type SecAssocResult = Result<Vec<SecAssocUpdate>, failure::Error>;
