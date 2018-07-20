// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod authenticator;
mod supplicant;

use self::authenticator::Authenticator;
use self::supplicant::Supplicant;
use Error;
use akm::Akm;
use bytes::BytesMut;
use cipher::{Cipher, GROUP_CIPHER_SUITE, TKIP};
use eapol;
use failure;
use key::exchange::Key;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::{Role, SecAssocResult, SecAssocStatus, SecAssocUpdate};
use rsne::Rsne;
use std::rc::Rc;

enum RoleHandler {
    Authenticator(Authenticator),
    Supplicant(Supplicant),
}

#[derive(Debug)]
pub enum MessageNumber {
    Message1 = 1,
    Message2 = 2,
    Message3 = 3,
    Message4 = 4,
}

#[derive(Debug, Clone)]
pub struct Config {
    pub role: Role,
    pub s_addr: [u8; 6],
    pub s_rsne: Rsne,
    pub a_addr: [u8; 6],
    pub a_rsne: Rsne,
}

impl Config {
    pub fn new(
        role: Role,
        s_addr: [u8; 6],
        s_rsne: Rsne,
        a_addr: [u8; 6],
        a_rsne: Rsne,
    ) -> Result<Config, failure::Error> {
        // TODO(hahnr): Validate configuration for:
        // (1) Correct RSNE subset
        // (2) Correct AKM and Cipher Suite configuration
        Ok(Config {
            role,
            s_addr,
            s_rsne,
            a_addr,
            a_rsne,
        })
    }
}

pub struct Fourway {
    cfg: Rc<Config>,
    handler: RoleHandler,
    ptk: Option<Ptk>,
    gtk: Option<Gtk>,
}

impl Fourway {
    pub fn new(cfg: Config, pmk: Vec<u8>) -> Result<Fourway, failure::Error> {
        let shared_cfg = Rc::new(cfg);
        let handler = match &shared_cfg.role {
            &Role::Supplicant => RoleHandler::Supplicant(Supplicant::new(shared_cfg.clone(), pmk)?),
            &Role::Authenticator => RoleHandler::Authenticator(Authenticator::new()?),
        };
        Ok(Fourway {
            cfg: shared_cfg,
            handler: handler,
            ptk: None,
            gtk: None,
        })
    }

    fn on_key_confirmed(&mut self, key: Key) {
        match key {
            Key::Ptk(ptk) => self.ptk = Some(ptk),
            Key::Gtk(gtk) => self.gtk = Some(gtk),
            _ => (),
        }
    }

    pub fn on_eapol_key_frame(&mut self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // (1) Validate frame.
        let () = self.validate_eapol_key_frame(frame)?;

        // (2) Decrypt key data.
        let mut plaintext = None;
        if frame.key_info.encrypted_key_data() {
            let rsne = &self.cfg.s_rsne;
            let akm = &rsne.akm_suites[0];
            let unwrap_result = match self.ptk.as_ref() {
                // Return error if key data is encrypted but the PTK was not yet derived.
                None => Err(Error::UnexpectedEncryptedKeyData.into()),
                // Else attempt to decrypt key data.
                Some(ptk) => akm.keywrap_algorithm()
                    .ok_or(Error::UnsupportedAkmSuite)?
                    .unwrap(ptk.kek(), &frame.key_data[..])
                    .map(|p| Some(p)),
            };
            plaintext = match unwrap_result {
                Ok(plaintext) => plaintext,
                Err(Error::WrongAesKeywrapKey) => {
                    return Ok(vec![SecAssocUpdate::Status(SecAssocStatus::WrongPassword)])
                },
                Err(e) => return Err(e.into())
            }
        }
        let key_data = match plaintext.as_ref() {
            Some(data) => &data[..],
            // Key data was never encrypted.
            None => &frame.key_data[..],
        };

        // (3) Process frame by handler.
        let result = match &mut self.handler {
            &mut RoleHandler::Authenticator(ref a) => a.on_eapol_key_frame(frame, key_data),
            &mut RoleHandler::Supplicant(ref mut s) => s.on_eapol_key_frame(frame, key_data),
        }?;

        // (4) Process results from handler.
        self.process_updates(result)
    }

    fn validate_eapol_key_frame(&self, frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
        // IEEE Std 802.1X-2010, 11.9
        let key_descriptor = match eapol::KeyDescriptor::from_u8(frame.descriptor_type) {
            Some(eapol::KeyDescriptor::Ieee802dot11) => Ok(eapol::KeyDescriptor::Ieee802dot11),
            // Use of RC4 is deprecated.
            Some(_) => Err(Error::InvalidKeyDescriptor(
                frame.descriptor_type,
                eapol::KeyDescriptor::Ieee802dot11,
            )),
            // Invalid value.
            None => Err(Error::UnsupportedKeyDescriptor(frame.descriptor_type)),
        }.map_err(|e| failure::Error::from(e))?;

        // IEEE Std 802.11-2016, 12.7.2 b.1)
        let rsne = &self.cfg.s_rsne;
        let expected_version = derive_key_descriptor_version(rsne, key_descriptor);
        if frame.key_info.key_descriptor_version() != expected_version {
            return Err(Error::UnsupportedKeyDescriptorVersion(
                frame.key_info.key_descriptor_version(),
            ).into());
        }

        // IEEE Std 802.11-2016, 12.7.2 b.2)
        // Only PTK derivation is supported as of now.
        if frame.key_info.key_type() != eapol::KEY_TYPE_PAIRWISE {
            return Err(Error::UnsupportedKeyDerivation.into());
        }

        // Drop messages which were not expected by the configured role.
        let msg_no = message_number(frame);
        match self.cfg.role {
            // Authenticator should only receive message 2 and 4.
            Role::Authenticator => match msg_no {
                MessageNumber::Message2 | MessageNumber::Message4 => Ok(()),
                _ => Err(Error::Unexpected4WayHandshakeMessage(msg_no)),
            },
            Role::Supplicant => match msg_no {
                MessageNumber::Message1 | MessageNumber::Message3 => Ok(()),
                _ => Err(Error::Unexpected4WayHandshakeMessage(msg_no)),
            },
        }.map_err(|e| failure::Error::from(e))?;

        // Explicit validation based on the frame's message number.
        let msg_no = message_number(frame);
        match msg_no {
            MessageNumber::Message1 => validate_message_1(frame),
            MessageNumber::Message2 => validate_message_2(frame),
            MessageNumber::Message3 => validate_message_3(frame),
            MessageNumber::Message4 => validate_message_4(frame),
        }?;

        // IEEE Std 802.11-2016, 12.7.2 c)
        match msg_no {
            MessageNumber::Message1 | MessageNumber::Message3 => {
                let rsne = &self.cfg.s_rsne;
                let pairwise = &rsne.pairwise_cipher_suites[0];
                let tk_bits = pairwise
                    .tk_bits()
                    .ok_or(Error::UnsupportedCipherSuite)
                    .map_err(|e| failure::Error::from(e))?;
                if frame.key_len != tk_bits / 8 {
                    Err(Error::InvalidPairwiseKeyLength(frame.key_len, tk_bits / 8))
                } else {
                    Ok(())
                }
            }
            _ => Ok(()),
        }.map_err(|e| failure::Error::from(e))?;

        // IEEE Std 802.11-2016, 12.7.2, d)
        let min_key_replay_counter = match &self.handler {
            &RoleHandler::Authenticator(ref a) => a.key_replay_counter,
            &RoleHandler::Supplicant(ref s) => s.key_replay_counter(),
        };
        if frame.key_replay_counter <= min_key_replay_counter {
            return Err(Error::InvalidKeyReplayCounter.into());
        }

        // IEEE Std 802.11-2016, 12.7.2, e)
        // Nonce is validated based on the frame's message number.
        // Must not be zero in 1st, 2nd and 3rd message.
        // Nonce in 3rd message must be same as the one from the 1st message.
        if let MessageNumber::Message3 = msg_no {
            let nonce_match = match &self.handler {
                &RoleHandler::Supplicant(ref s) => &frame.key_nonce[..] == s.anonce(),
                _ => false,
            };
            if !nonce_match {
                return Err(Error::ErrorNonceDoesntMatch.into());
            }
        }

        // IEEE Std 802.11-2016, 12.7.2, g)
        // Key RSC validated based on the frame's message number.
        // Optional in the 3rd message. Must be zero in others.

        // IEEE Std 802.11-2016, 12.7.2 h)
        let rsne = &self.cfg.s_rsne;
        let akm = &rsne.akm_suites[0];
        let mic_bytes = akm.mic_bytes()
            .ok_or(Error::UnsupportedAkmSuite)
            .map_err(|e| failure::Error::from(e))?;
        if frame.key_mic.len() != mic_bytes as usize {
            return Err(Error::InvalidMicSize.into());
        }

        // If a MIC is set but the PTK was not yet derived, the MIC cannot be verified.
        if frame.key_info.key_mic() {
            match self.ptk.as_ref() {
                None => Err(Error::UnexpectedMic.into()),
                Some(ptk) => {
                    let mut buf = Vec::with_capacity(frame.len());
                    frame.as_bytes(true, &mut buf);
                    let written = buf.len();
                    let valid_mic = akm.integrity_algorithm()
                        .ok_or(Error::UnsupportedAkmSuite)?
                        .verify(ptk.kck(), &buf[..], &frame.key_mic[..]);
                    if !valid_mic {
                        Err(Error::InvalidMic)
                    } else {
                        Ok(())
                    }
                }
            }.map_err(|e: Error| failure::Error::from(e))?;
        }

        // IEEE Std 802.11-2016, 12.7.2 i) & j)
        if frame.key_data_len as usize != frame.key_data.len() {
            return Err(Error::InvalidKeyDataLength.into());
        }
        Ok(())
    }

    fn process_updates(&mut self, mut updates: Vec<SecAssocUpdate>) -> SecAssocResult {
        // Filter key updates and process ourselves to prevent reporting keys before the Handshake
        // completed successfully. Report all other updates.
        updates
            .drain_filter(|update| match update {
                SecAssocUpdate::Key(_) => true,
                _ => false,
            })
            .for_each(|update| {
                if let SecAssocUpdate::Key(key) = update {
                    self.on_key_confirmed(key);
                }
            });

        // If both PTK and GTK are known the Handshake completed successfully and keys can be
        // reported.
        if let (Some(ptk), Some(gtk)) = (self.ptk.as_ref(), self.gtk.as_ref()) {
            updates.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
            updates.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
        }
        Ok(updates)
    }
}

// Verbose and explicit validation of Message 1 to 4.

fn validate_message_1(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    if frame.key_info.install() {
        Err(Error::InvalidInstallBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    } else if !frame.key_info.key_ack() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    } else if frame.key_info.key_mic() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    } else if frame.key_info.secure() {
        Err(Error::InvalidSecureBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    } else if frame.key_info.error() {
        Err(Error::InvalidErrorBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    } else if frame.key_info.request() {
        Err(Error::InvalidRequestBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    } else if frame.key_info.encrypted_key_data() {
        Err(Error::InvalidEncryptedKeyDataBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 e)
    } else if is_zero(&frame.key_nonce[..]) {
        Err(Error::InvalidNonce.into())
    // IEEE Std 802.11-2016, 12.7.6.2
    } else if !is_zero(&frame.key_iv[..]) {
        Err(Error::InvalidIv.into())
    // IEEE Std 802.11-2016, 12.7.2 g)
    } else if frame.key_rsc != 0 {
        Err(Error::InvalidRsc.into())
    } else {
        Ok(())
    }

    // The first message of the Handshake is also required to carry a zeroed MIC.
    // Some routers however send messages without zeroing out the MIC beforehand.
    // To ensure compatibility with such routers, the MIC of the first message is
    // allowed to be set.
    // This assumption faces no security risk because the message's MIC is only
    // validated in the Handshake and not in the Supplicant or Authenticator
    // implementation.
}

fn validate_message_2(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    if frame.key_info.install() {
        Err(Error::InvalidInstallBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    } else if frame.key_info.key_ack() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    } else if !frame.key_info.key_mic() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    } else if frame.key_info.secure() {
        Err(Error::InvalidSecureBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    } else if frame.key_info.error() {
        Err(Error::InvalidErrorBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    } else if frame.key_info.request() {
        Err(Error::InvalidRequestBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    } else if frame.key_info.encrypted_key_data() {
        Err(Error::InvalidEncryptedKeyDataBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 e)
    } else if is_zero(&frame.key_nonce[..]) {
        Err(Error::InvalidNonce.into())
    // IEEE Std 802.11-2016, 12.7.6.3
    } else if !is_zero(&frame.key_iv[..]) {
        Err(Error::InvalidIv.into())
    // IEEE Std 802.11-2016, 12.7.2 g)
    } else if frame.key_rsc != 0 {
        Err(Error::InvalidRsc.into())
    } else {
        Ok(())
    }
}

fn validate_message_3(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    // Install = 0 is only used in key mapping with TKIP and WEP, neither is supported by Fuchsia.
    if !frame.key_info.install() {
        Err(Error::InvalidInstallBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    } else if !frame.key_info.key_ack() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    } else if !frame.key_info.key_mic() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    } else if !frame.key_info.secure() {
        Err(Error::InvalidSecureBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    } else if frame.key_info.error() {
        Err(Error::InvalidErrorBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    } else if frame.key_info.request() {
        Err(Error::InvalidRequestBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    } else if !frame.key_info.encrypted_key_data() {
        Err(Error::InvalidEncryptedKeyDataBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 e)
    } else if is_zero(&frame.key_nonce[..]) {
        Err(Error::InvalidNonce.into())
    // IEEE Std 802.11-2016, 12.7.6.4
    } else if frame.version >= eapol::ProtocolVersion::Ieee802dot1x2004 as u8 &&
        !is_zero(&frame.key_iv[..]) {
        Err(Error::InvalidIv.into())
    // IEEE Std 802.11-2016, 12.7.2 i) & j)
    // Key Data must not be empty.
    } else if frame.key_data_len == 0 {
        Err(Error::EmptyKeyData.into())
    } else {
        Ok(())
    }
}

fn validate_message_4(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    if frame.key_info.install() {
        Err(Error::InvalidInstallBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    } else if frame.key_info.key_ack() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    } else if !frame.key_info.key_mic() {
        Err(Error::InvalidKeyAckBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    } else if !frame.key_info.secure() {
        Err(Error::InvalidSecureBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    } else if frame.key_info.error() {
        Err(Error::InvalidErrorBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    } else if frame.key_info.request() {
        Err(Error::InvalidRequestBitValue.into())
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    } else if frame.key_info.encrypted_key_data() {
        Err(Error::InvalidEncryptedKeyDataBitValue.into())
    // IEEE Std 802.11-2016, 12.7.6.5
    } else if !is_zero(&frame.key_iv[..]) {
        Err(Error::InvalidIv.into())
    // IEEE Std 802.11-2016, 12.7.2 g)
    } else if frame.key_rsc != 0 {
        Err(Error::InvalidRsc.into())
    } else {
        Ok(())
    }
}

fn message_number(rx_frame: &eapol::KeyFrame) -> MessageNumber {
    // IEEE does not specify how to determine a frame's message number in the 4-Way Handshake
    // sequence. However, it's important to know a frame's message number to do further
    // validations. To derive the message number the key info field is used.
    // 4-Way Handshake specific EAPOL Key frame requirements:
    // IEEE Std 802.11-2016, 12.7.6.1

    // IEEE Std 802.11-2016, 12.7.6.2 & 12.7.6.4
    // Authenticator requires acknowledgement of all its sent frames.
    if rx_frame.key_info.key_ack() {
        // Authenticator only sends 1st and 3rd message of the handshake.
        // IEEE Std 802.11-2016, 12.7.2 b.4)
        // The third requires key installation while the first one doesn't.
        if rx_frame.key_info.install() {
            MessageNumber::Message3
        } else {
            MessageNumber::Message1
        }
    } else {
        // Supplicant only sends 2nd and 4th message of the handshake.
        // IEEE Std 802.11-2016, 12.7.2 b.7)
        // The fourth message is secured while the second one is not.
        if rx_frame.key_info.secure() {
            MessageNumber::Message4
        } else {
            MessageNumber::Message2
        }
    }
}

// IEEE Std 802.11-2016, 12.7.2 b.1)
// Key Descriptor Version is based on the negotiated AKM, Pairwise- and Group Cipher suite.
fn derive_key_descriptor_version(rsne: &Rsne, key_descriptor_type: eapol::KeyDescriptor) -> u16 {
    let akm = &rsne.akm_suites[0];
    let pairwise = &rsne.pairwise_cipher_suites[0];

    if !akm.has_known_algorithm() || !pairwise.has_known_usage() {
        return 0;
    }

    match akm.suite_type {
        1 | 2 => match key_descriptor_type {
            eapol::KeyDescriptor::Rc4 => match pairwise.suite_type {
                TKIP | GROUP_CIPHER_SUITE => 1,
                _ => 0,
            },
            eapol::KeyDescriptor::Ieee802dot11 => {
                if pairwise.is_enhanced() {
                    2
                } else {
                    match rsne.group_data_cipher_suite.as_ref() {
                        Some(group) if group.is_enhanced() => 2,
                        _ => 0,
                    }
                }
            }
            _ => 0,
        },
        // Interestingly, IEEE 802.11 does not specify any pairwise- or group cipher
        // requirements for these AKMs.
        3...6 => 3,
        _ => 0,
    }
}

fn is_zero(slice: &[u8]) -> bool {
    slice.iter().all(|&x| x == 0)
}


#[cfg(test)]
mod tests {
    use super::*;
    use rsna::test_util::{self, MessageOverride};
    use bytes::Bytes;

    fn make_handshake() -> Fourway {
        // Create a new instance of the 4-Way Handshake in Supplicant role.
        let pmk = test_util::get_pmk();
        let cfg = Config{
            s_rsne: test_util::get_s_rsne(),
            a_rsne: test_util::get_a_rsne(),
            s_addr: test_util::S_ADDR,
            a_addr: test_util::A_ADDR,
            role: Role::Supplicant
        };
        Fourway::new(cfg, pmk).expect("error while creating 4-Way Handshake")
    }

    fn compute_ptk(a_nonce: &[u8], supplicant_result: SecAssocResult) -> Option<Ptk> {
        match supplicant_result.expect("expected successful processing of first message")
            .get(0)
            .expect("expected at least one response from Supplicant")
            {
                SecAssocUpdate::TxEapolKeyFrame(msg2) => {
                    let snonce = msg2.key_nonce;
                    let derived_ptk = test_util::get_ptk(a_nonce, &snonce[..]);
                    Some(derived_ptk)
                }
                _ => None,
            }
    }

    #[test]
    fn test_nonzero_mic_in_first_msg() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and verify result.
        let a_nonce = test_util::get_nonce();
        let mut msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        msg1.key_mic = Bytes::from(vec![0xAA; 16]);
        let result = handshake.on_eapol_key_frame(&msg1);
        assert!(result.is_ok(), "error, expected success for processing first msg but result is: {:?}", result);
    }

    // First messages of 4-Way Handshake must carry a zeroed IV in all protocol versions.

    #[test]
    fn test_random_iv_msg1_v1() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and verify result.
        let a_nonce = test_util::get_nonce();
        let mut msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        msg1.version = 1;
        msg1.key_iv[0] = 0xFF;
        let result = handshake.on_eapol_key_frame(&msg1);
        assert!(result.is_err(), "error, expected failure for first msg but result is: {:?}", result);
    }

    #[test]
    fn test_random_iv_msg1_v2() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and verify result.
        let a_nonce = test_util::get_nonce();
        let mut msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        msg1.version = 2;
        msg1.key_iv[0] = 0xFF;
        let result = handshake.on_eapol_key_frame(&msg1);
        assert!(result.is_err(), "error, expected failure for first msg but result is: {:?}", result);
    }

    // EAPOL Key frames can carry a random IV in the third message of the 4-Way Handshake if
    // protocol version 1, 802.1X-2001, is used. All other protocol versions require a zeroed IV
    // for the third message of the handshake.

    #[test]
    fn test_random_iv_msg3_v1() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and derive PTK.
        let a_nonce = test_util::get_nonce();
        let msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        let result = handshake.on_eapol_key_frame(&msg1);
        let ptk = compute_ptk(&a_nonce[..], result).expect("could not derive PTK");

        // Send third message of 4-Way Handshake to Supplicant.
        let gtk = vec![42u8; 16];
        let mut msg3 = test_util::get_4whs_msg3(&ptk, &a_nonce[..], &gtk[..], Some(MessageOverride{
            version: 1,
            replay_counter: 2,
            iv: [0xFFu8; 16],
        }));
        let result = handshake.on_eapol_key_frame(&msg3);
        assert!(result.is_ok(), "error, expected success for processing third msg but result is: {:?}", result);
    }

    #[test]
    fn test_random_iv_msg3_v2() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and derive PTK.
        let a_nonce = test_util::get_nonce();
        let msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        let result = handshake.on_eapol_key_frame(&msg1);
        let ptk = compute_ptk(&a_nonce[..], result).expect("could not derive PTK");

        // Send third message of 4-Way Handshake to Supplicant.
        let gtk = vec![42u8; 16];
        let mut msg3 = test_util::get_4whs_msg3(&ptk, &a_nonce[..], &gtk[..], Some(MessageOverride{
            version: 2,
            replay_counter: 2,
            iv: [0xFFu8; 16],
        }));
        let result = handshake.on_eapol_key_frame(&msg3);
        assert!(result.is_err(), "error, expected failure for third msg but result is: {:?}", result);
    }

    #[test]
    fn test_zeroed_iv_msg3_v2() {
        let mut handshake = make_handshake();

        // Send first message of Handshake to Supplicant and derive PTK.
        let a_nonce = test_util::get_nonce();
        let msg1 = test_util::get_4whs_msg1(&a_nonce[..], 1);
        let result = handshake.on_eapol_key_frame(&msg1);
        let ptk = compute_ptk(&a_nonce[..], result).expect("could not derive PTK");

        // Send third message of 4-Way Handshake to Supplicant.
        let gtk = vec![42u8; 16];
        let mut msg3 = test_util::get_4whs_msg3(&ptk, &a_nonce[..], &gtk[..], Some(MessageOverride{
            version: 2,
            replay_counter: 2,
            iv: [0u8; 16],
        }));
        let result = handshake.on_eapol_key_frame(&msg3);
        assert!(result.is_ok(), "error, expected success for processing third msg but result is: {:?}", result);
    }

    // TODO(hahnr): Add additional tests.
}
