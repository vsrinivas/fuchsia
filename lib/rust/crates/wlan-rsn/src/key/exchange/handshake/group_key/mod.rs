// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use akm::Akm;
use bytes::Bytes;
use cipher::Cipher;
use eapol;
use failure;
use key::{exchange::{self, handshake::group_key::supplicant::Supplicant}, ptk::Ptk};
use rsna::{Role, SecAssocResult, VerifiedKeyFrame};

mod supplicant;

#[derive(Debug, PartialEq)]
enum RoleHandler {
    Supplicant(Supplicant),
}

// Struct which carries EAPOL key frames which comply with IEEE Std 802.11-2016, 12.7.2 and
// IEEE Std 802.11-2016, 12.7.7.
pub struct GroupKeyHandshakeFrame<'a> {
    frame: &'a eapol::KeyFrame,
    kd_plaintext: Bytes,
}

impl <'a> GroupKeyHandshakeFrame<'a> {

    pub fn from_verified(valid_frame: VerifiedKeyFrame<'a>, role: Role)
        -> Result<GroupKeyHandshakeFrame<'a>, failure::Error>
    {
        let sender = match role {
            Role::Supplicant => Role::Authenticator,
            Role::Authenticator => Role::Supplicant,
        };

        let frame = valid_frame.get();
        let kd_plaintext = valid_frame.key_data_plaintext();

        // IEEE Std 802.11-2016, 12.7.7.2 & IEEE Std 802.11-2016, 12.7.7.3
        ensure!(frame.key_info.key_type() == eapol::KEY_TYPE_GROUP_SMK,
                "only group key messages are allowed in Group Key Handshake");
        ensure!(!frame.key_info.install(), "installbit must not be set in Group Key Handshake messages");
        ensure!(frame.key_info.key_mic(), "MIC bit must be set in Group Key Handshake messages");
        ensure!(frame.key_info.secure(), "secure bit must be set in Group Key Handshake messages");
        ensure!(!frame.key_info.error(), "error bit must not be set in Group Key Handshake messages");
        ensure!(!frame.key_info.request(), "request bit must not be set in Group Key Handshake messages");

        // IEEE Std 802.11-2016, 12.7.7.2 requires the key length to be set to 0 in
        // group key handshakes messages. Some routers, such as Apple Airport Extreme
        // violate this specification and do send the pairwise cipher key length in group
        // key messages as well. To support interoperability, the key length is allowed to
        // be arbitrary as it's not used in the Group Key handshake.

        // IEEE Std 802.11-2016, 12.7.7.2 requires the nonce to be zeroed in group key handshakes
        // messages. Some routers, such as Apple Airport Extreme violate this specification and do
        // send a nonce other than zero. To support interoperability, the nonce is allowed to
        // be an arbitrary value as it's not used in the Group Key handshake.


        match &sender {
            // IEEE Std 802.11-2016, 12.7.7.2
            Role::Authenticator => {
                ensure!(frame.key_info.key_ack(), "ACK bit must be set in 1st message of Group Key Handshake");
                ensure!(frame.key_info.encrypted_key_data(), "encrypted data bit must be set in 1st message of Group Key Handshake");
                ensure!(frame.version == eapol::ProtocolVersion::Ieee802dot1x2001 as u8 ||
                        is_zero(&frame.key_iv[..]),
                        "invalid IV in 1st message of Group Key Handshake with version: {}", frame.version);
                // RSC is currently not taken into account.
            },
            // IEEE Std 802.11-2016, 12.7.7.3
            Role::Supplicant => {
                ensure!(!frame.key_info.key_ack(), "ACK bit must not be set in 2nd message of Group Key Handshake");
                ensure!(!frame.key_info.encrypted_key_data(), "encrypted data bit must not be set in 2nd message of Group Key Handshake");
                ensure!(is_zero(&frame.key_iv[..]), "IV must be zero in 2nd message of Group Key Handshake");
                ensure!(frame.key_rsc == 0, "RSC must be zero in 2nd message of Group Key Handshake");
            },
        };

        Ok(GroupKeyHandshakeFrame{ frame, kd_plaintext: Bytes::from(kd_plaintext) })
    }

    pub fn get(&self) -> &'a eapol::KeyFrame {
        self.frame
    }

    pub fn key_data_plaintext(&self) -> &[u8] {
        &self.kd_plaintext[..]
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub role: Role,
    pub akm: Akm,
}

#[derive(Debug, PartialEq)]
pub struct GroupKey(RoleHandler);

impl GroupKey {
    pub fn new(cfg: Config, kck: &[u8]) -> Result<GroupKey, failure::Error> {
        let handler = match &cfg.role {
            Role::Supplicant => RoleHandler::Supplicant(Supplicant{cfg, kck: Bytes::from(kck)}),
            _ => bail!("Authenticator not yet support in Group-Key Handshake"),
        };

        Ok(GroupKey(handler))
    }

    pub fn destroy(self) -> exchange::Config  {
        match self.0 {
            RoleHandler::Supplicant(s) => exchange::Config::GroupKeyHandshake(s.destroy()),
        }
    }

    pub fn on_eapol_key_frame(&mut self, frame: VerifiedKeyFrame) -> SecAssocResult {
        match &mut self.0 {
            RoleHandler::Supplicant(s) => {
                let frame = GroupKeyHandshakeFrame::from_verified(frame, Role::Supplicant)?;
                s.on_eapol_key_frame(frame)
            },
        }
    }
}

fn is_zero(slice: &[u8]) -> bool {
    slice.iter().all(|&x| x == 0)
}