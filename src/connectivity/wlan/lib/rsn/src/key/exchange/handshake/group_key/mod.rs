// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::{self, handshake::group_key::supplicant::Supplicant};
use crate::rsna::{KeyFrameKeyDataState, KeyFrameState, Role, UpdateSink, VerifiedKeyFrame};
use bytes::Bytes;
use eapol;
use failure::{self, bail, ensure};
use wlan_common::ie::rsn::{akm::Akm, cipher::Cipher};

mod supplicant;

#[derive(Debug, PartialEq)]
enum RoleHandler {
    Supplicant(Supplicant),
}

// Struct which carries EAPOL key frames which comply with IEEE Std 802.11-2016, 12.7.2 and
// IEEE Std 802.11-2016, 12.7.7.
pub struct GroupKeyHandshakeFrame<'a> {
    frame: &'a eapol::KeyFrame,
}

impl<'a> GroupKeyHandshakeFrame<'a> {
    pub fn from_verified(
        valid_frame: VerifiedKeyFrame<'a>,
        role: Role,
    ) -> Result<GroupKeyHandshakeFrame<'a>, failure::Error> {
        // Safe since the frame will be wrapped again in a `KeyFrameState` when being accessed.
        let frame = valid_frame.get().unsafe_get_raw();

        let sender = match role {
            Role::Supplicant => Role::Authenticator,
            Role::Authenticator => Role::Supplicant,
        };

        // IEEE Std 802.11-2016, 12.7.7.2 & IEEE Std 802.11-2016, 12.7.7.3
        ensure!(
            frame.key_info.key_type() == eapol::KEY_TYPE_GROUP_SMK,
            "only group key messages are allowed in Group Key Handshake"
        );
        ensure!(
            !frame.key_info.install(),
            "installbit must not be set in Group Key Handshake messages"
        );
        ensure!(frame.key_info.key_mic(), "MIC bit must be set in Group Key Handshake messages");
        ensure!(frame.key_info.secure(), "secure bit must be set in Group Key Handshake messages");
        ensure!(
            !frame.key_info.error(),
            "error bit must not be set in Group Key Handshake messages"
        );
        ensure!(
            !frame.key_info.request(),
            "request bit must not be set in Group Key Handshake messages"
        );

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
                ensure!(
                    frame.key_info.key_ack(),
                    "ACK bit must be set in 1st message of Group Key Handshake"
                );
                ensure!(
                    frame.key_info.encrypted_key_data(),
                    "encrypted data bit must be set in 1st message of Group Key Handshake"
                );

                // IEEE Std 802.11-2016, 12.7.7.2 requires the key frame's IV to be zero'ed when
                // using 802.1X-2004. Some routers, such as Linksys, violate this constraint.
                // The IV is not used in the Group Key Handshake, thus, ignoring this requirement is
                // safe.

                // RSC is currently not taken into account.
            }
            // IEEE Std 802.11-2016, 12.7.7.3
            Role::Supplicant => {
                ensure!(
                    !frame.key_info.key_ack(),
                    "ACK bit must not be set in 2nd message of Group Key Handshake"
                );
                ensure!(
                    !frame.key_info.encrypted_key_data(),
                    "encrypted data bit must not be set in 2nd message of Group Key Handshake"
                );
                ensure!(
                    is_zero(&frame.key_iv[..]),
                    "IV must be zero in 2nd message of Group Key Handshake"
                );
                ensure!(
                    frame.key_rsc == 0,
                    "RSC must be zero in 2nd message of Group Key Handshake"
                );
            }
        };

        Ok(GroupKeyHandshakeFrame { frame })
    }

    pub fn get(&self) -> KeyFrameState<'a> {
        KeyFrameState::from_frame(self.frame)
    }

    pub fn get_key_data(&self) -> KeyFrameKeyDataState<'a> {
        KeyFrameKeyDataState::from_frame(self.frame)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub role: Role,
    pub akm: Akm,
    pub cipher: Cipher,
}

#[derive(Debug, PartialEq)]
pub struct GroupKey(RoleHandler);

impl GroupKey {
    pub fn new(cfg: Config, kck: &[u8], kek: &[u8]) -> Result<GroupKey, failure::Error> {
        let handler = match &cfg.role {
            Role::Supplicant => RoleHandler::Supplicant(Supplicant {
                cfg,
                kck: Bytes::from(kck),
                kek: Bytes::from(kek),
            }),
            _ => bail!("Authenticator not yet support in Group-Key Handshake"),
        };

        Ok(GroupKey(handler))
    }

    pub fn destroy(self) -> exchange::Config {
        match self.0 {
            RoleHandler::Supplicant(s) => exchange::Config::GroupKeyHandshake(s.destroy()),
        }
    }

    pub fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        _key_replay_counter: u64,
        frame: VerifiedKeyFrame,
    ) -> Result<(), failure::Error> {
        match &mut self.0 {
            RoleHandler::Supplicant(s) => {
                let frame = GroupKeyHandshakeFrame::from_verified(frame, Role::Supplicant)?;
                s.on_eapol_key_frame(update_sink, frame)
            }
        }
    }
}

fn is_zero(slice: &[u8]) -> bool {
    slice.iter().all(|&x| x == 0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rsna::{test_util, NegotiatedRsne};

    fn verify_group_key_frame<'a>(
        key_frame: &'a eapol::KeyFrame,
        role: Role,
    ) -> Result<GroupKeyHandshakeFrame<'a>, failure::Error> {
        let rsne = NegotiatedRsne::from_rsne(&test_util::get_s_rsne()).expect("error getting RNSE");
        let frame = VerifiedKeyFrame::from_key_frame(&key_frame, &role, &rsne, 0)
            .expect("couldn't verify frame");
        GroupKeyHandshakeFrame::from_verified(frame, role)
    }

    fn fake_key_frame() -> eapol::KeyFrame {
        eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2004 as u8,
            packet_type: eapol::PacketType::Key as u8,
            descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
            key_info: eapol::KeyInformation(0b01001110000010),
            ..Default::default()
        }
    }

    #[test]
    fn zeroed_iv_8021x2004() {
        let key_frame = eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2004 as u8,
            ..fake_key_frame()
        };
        verify_group_key_frame(&key_frame, Role::Supplicant).expect("error verifying group frame");
    }

    #[test]
    fn random_iv_8021x2004() {
        let key_frame = eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2004 as u8,
            // IEEE does not allow random IV in combination with 802.1X-2004, however, not
            key_iv: [1; 16],
            ..fake_key_frame()
        };
        verify_group_key_frame(&key_frame, Role::Supplicant).expect("error verifying group frame");
    }

    #[test]
    fn zeroed_iv_8021x2001() {
        let key_frame = eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2001 as u8,
            ..fake_key_frame()
        };
        verify_group_key_frame(&key_frame, Role::Supplicant).expect("error verifying group frame");
    }

    #[test]
    fn random_iv_8021x2001() {
        let key_frame = eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2001 as u8,
            key_iv: [1; 16],
            ..fake_key_frame()
        };
        verify_group_key_frame(&key_frame, Role::Supplicant).expect("error verifying group frame");
    }
}
