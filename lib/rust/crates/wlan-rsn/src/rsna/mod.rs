// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use akm::Akm;
use bytes::Bytes;
use cipher::Cipher;
use eapol;
use failure;
use Error;
use rsne::Rsne;
use key::exchange::Key;

pub mod esssa;
#[cfg(test)]
pub mod test_util;

#[derive(Debug, Clone, PartialEq)]
pub struct NegotiatedRsne {
    group_data: Cipher,
    pairwise: Cipher,
    akm: Akm,
}

impl NegotiatedRsne {

    pub fn from_rsne(rsne: &Rsne) -> Result<NegotiatedRsne, failure::Error> {
        if rsne.group_data_cipher_suite.is_none() {
            return Err(Error::InvalidNegotiatedRsne.into())
        }
        if rsne.pairwise_cipher_suites.len() != 1 {
            return Err(Error::InvalidNegotiatedRsne.into())
        }
        if rsne.akm_suites.len() != 1 {
            return Err(Error::InvalidNegotiatedRsne.into())
        }

        Ok(NegotiatedRsne{
            group_data: rsne.group_data_cipher_suite.clone().unwrap(),
            pairwise: rsne.pairwise_cipher_suites[0].clone(),
            akm: rsne.akm_suites[0].clone(),
        })
    }

}

// EAPOL Key frames carried in this struct must comply with IEEE Std 802.11-2016, 12.7.2.
// TODO(hahnr): Enforce verification by making this struct only constructable through proper
// validation.
pub struct VerifiedKeyFrame<'a> {
    pub frame: &'a eapol::KeyFrame,
    pub kd_plaintext: Bytes,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Role {
    Authenticator,
    Supplicant,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocStatus {
    // TODO(hahnr): Rather than reporting wrong password as a status, report it as an error.
    WrongPassword,
    EssSaEstablished,
}

#[derive(Debug, PartialEq)]
pub enum SecAssocUpdate {
    TxEapolKeyFrame(eapol::KeyFrame),
    Key(Key),
    Status(SecAssocStatus),
}

pub type SecAssocResult = Result<Vec<SecAssocUpdate>, failure::Error>;
