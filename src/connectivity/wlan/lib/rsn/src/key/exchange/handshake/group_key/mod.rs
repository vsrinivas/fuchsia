// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::{self, handshake::group_key::supplicant::Supplicant};
use crate::rsna::{Dot11VerifiedKeyFrame, NegotiatedProtection, Role, UpdateSink};
use crate::{rsn_ensure, Error};
use bytes::Bytes;
use eapol;
use zerocopy::ByteSlice;

mod supplicant;

#[derive(Debug, PartialEq)]
enum RoleHandler {
    Supplicant(Supplicant),
}

// Struct which carries EAPOL key frames which comply with IEEE Std 802.11-2016, 12.7.2 and
// IEEE Std 802.11-2016, 12.7.7.
pub struct GroupKeyHandshakeFrame<B: ByteSlice> {
    frame: Dot11VerifiedKeyFrame<B>,
}

impl<B: ByteSlice> GroupKeyHandshakeFrame<B> {
    pub fn from_verified(frame: Dot11VerifiedKeyFrame<B>, role: Role) -> Result<Self, Error> {
        // Safe since the frame will be wrapped again in a `Dot11VerifiedKeyFrame` when being accessed.
        let raw_frame = frame.unsafe_get_raw();

        let sender = match role {
            Role::Supplicant => Role::Authenticator,
            Role::Authenticator => Role::Supplicant,
        };

        let key_info = raw_frame.key_frame_fields.key_info();

        // IEEE Std 802.11-2016, 12.7.7.2 & IEEE Std 802.11-2016, 12.7.7.3
        rsn_ensure!(
            key_info.key_type() == eapol::KeyType::GROUP_SMK,
            "only group key messages are allowed in Group Key Handshake"
        );
        rsn_ensure!(
            !key_info.install(),
            "installbit must not be set in Group Key Handshake messages"
        );
        rsn_ensure!(key_info.key_mic(), "MIC bit must be set in Group Key Handshake messages");
        rsn_ensure!(key_info.secure(), "secure bit must be set in Group Key Handshake messages");
        rsn_ensure!(!key_info.error(), "error bit must not be set in Group Key Handshake messages");
        rsn_ensure!(
            !key_info.request(),
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
                rsn_ensure!(
                    key_info.key_ack(),
                    "ACK bit must be set in 1st message of Group Key Handshake"
                );

                // WFA, WPA1 Spec. 3.1, Chapter 2.2.4
                // WPA1 does not use the encrypted key data bit, so we don't bother checking it.
                if raw_frame.key_frame_fields.descriptor_type != eapol::KeyDescriptor::LEGACY_WPA1 {
                    rsn_ensure!(
                        key_info.encrypted_key_data(),
                        "encrypted data bit must be set in 1st message of Group Key Handshake"
                    );
                }

                // IEEE Std 802.11-2016, 12.7.7.2 requires the key frame's IV to be zero'ed when
                // using 802.1X-2004. Some routers, such as Linksys, violate this constraint.
                // The IV is not used in the Group Key Handshake, thus, ignoring this requirement is
                // safe.

                // RSC is currently not taken into account.
            }
            // IEEE Std 802.11-2016, 12.7.7.3
            Role::Supplicant => {
                rsn_ensure!(
                    !key_info.key_ack(),
                    "ACK bit must not be set in 2nd message of Group Key Handshake"
                );
                rsn_ensure!(
                    !key_info.encrypted_key_data(),
                    "encrypted data bit must not be set in 2nd message of Group Key Handshake"
                );
                rsn_ensure!(
                    is_zero(&raw_frame.key_frame_fields.key_iv[..]),
                    "IV must be zero in 2nd message of Group Key Handshake"
                );
                rsn_ensure!(
                    raw_frame.key_frame_fields.key_rsc.to_native() == 0,
                    "RSC must be zero in 2nd message of Group Key Handshake"
                );
            }
        };

        Ok(GroupKeyHandshakeFrame { frame })
    }

    pub fn get(self) -> Dot11VerifiedKeyFrame<B> {
        self.frame
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub role: Role,
    pub protection: NegotiatedProtection,
}

#[derive(Debug, PartialEq)]
pub struct GroupKey(RoleHandler);

impl GroupKey {
    pub fn new(cfg: Config, kck: &[u8], kek: &[u8]) -> Result<GroupKey, Error> {
        let handler = match &cfg.role {
            Role::Supplicant => RoleHandler::Supplicant(Supplicant {
                cfg,
                kck: Bytes::copy_from_slice(kck),
                kek: Bytes::copy_from_slice(kek),
            }),
            _ => {
                return Err(Error::GenericError(
                    "Authenticator not yet support in Group-Key Handshake".to_string(),
                ))
            }
        };

        Ok(GroupKey(handler))
    }

    pub fn destroy(self) -> exchange::Config {
        match self.0 {
            RoleHandler::Supplicant(s) => exchange::Config::GroupKeyHandshake(s.destroy()),
        }
    }

    pub fn on_eapol_key_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        _key_replay_counter: u64,
        frame: Dot11VerifiedKeyFrame<B>,
    ) -> Result<(), Error> {
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
    use crate::rsna::{test_util, NegotiatedProtection};

    fn verify_group_key_frame(key_frame: eapol::KeyFrameBuf, role: Role) {
        let protection =
            NegotiatedProtection::from_rsne(&test_util::get_s_rsne()).expect("error getting RNSE");
        let parsed_frame = eapol::KeyFrameRx::parse(test_util::mic_len(), &key_frame[..])
            .expect("failed to parse group key frame");
        let frame = Dot11VerifiedKeyFrame::from_frame(parsed_frame, &role, &protection, 0)
            .expect("couldn't verify frame");
        GroupKeyHandshakeFrame::from_verified(frame, role).expect("error verifying group_frame");
    }

    fn fake_key_frame() -> eapol::KeyFrameTx {
        let mut key_frame_fields = eapol::KeyFrameFields::default();
        key_frame_fields.descriptor_type = eapol::KeyDescriptor::IEEE802DOT11;
        key_frame_fields.set_key_info(eapol::KeyInformation(0b01001110000010));
        eapol::KeyFrameTx::new(
            eapol::ProtocolVersion::IEEE802DOT1X2004,
            key_frame_fields,
            vec![],
            test_util::mic_len(),
        )
    }

    #[test]
    fn zeroed_iv_8021x2004() {
        let mut key_frame = fake_key_frame();
        key_frame.protocol_version = eapol::ProtocolVersion::IEEE802DOT1X2004;
        let key_frame = key_frame
            .serialize()
            .finalize_with_mic(&vec![0u8; test_util::mic_len()][..])
            .expect("failed to construct key frame");
        verify_group_key_frame(key_frame, Role::Supplicant);
    }

    #[test]
    fn random_iv_8021x2004() {
        let mut key_frame = fake_key_frame();
        key_frame.protocol_version = eapol::ProtocolVersion::IEEE802DOT1X2004;
        // IEEE does not allow random IV in combination with 802.1X-2004.
        key_frame.key_frame_fields.key_iv.copy_from_slice(&[1; 16]);
        let key_frame = key_frame
            .serialize()
            .finalize_with_mic(&vec![0u8; test_util::mic_len()][..])
            .expect("failed to construct key frame");
        verify_group_key_frame(key_frame, Role::Supplicant);
    }

    #[test]
    fn zeroed_iv_8021x2001() {
        let mut key_frame = fake_key_frame();
        key_frame.protocol_version = eapol::ProtocolVersion::IEEE802DOT1X2001;
        let key_frame = key_frame
            .serialize()
            .finalize_with_mic(&vec![0u8; test_util::mic_len()][..])
            .expect("failed to construct key frame");
        verify_group_key_frame(key_frame, Role::Supplicant);
    }

    #[test]
    fn random_iv_8021x2001() {
        let mut key_frame = fake_key_frame();
        key_frame.protocol_version = eapol::ProtocolVersion::IEEE802DOT1X2001;
        key_frame.key_frame_fields.key_iv.copy_from_slice(&[1; 16]);
        let key_frame = key_frame
            .serialize()
            .finalize_with_mic(&vec![0u8; test_util::mic_len()][..])
            .expect("failed to construct key frame");
        verify_group_key_frame(key_frame, Role::Supplicant);
    }
}
