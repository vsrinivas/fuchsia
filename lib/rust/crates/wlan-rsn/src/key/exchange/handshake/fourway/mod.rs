// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod authenticator;
mod supplicant;

use crate::key::exchange;
use crate::rsna::{NegotiatedRsne, Role, VerifiedKeyFrame, UpdateSink, KeyFrameState, KeyFrameKeyDataState, SecAssocUpdate};
use crate::rsne::Rsne;
use crate::state_machine::StateMachine;
use crate::Error;
use crate::crypto_utils::nonce::NonceReader;
use eapol;
use failure::{self, bail, ensure};

#[derive(Debug, PartialEq)]
pub enum MessageNumber {
    Message1 = 1,
    Message2 = 2,
    Message3 = 3,
    Message4 = 4,
}

// Struct which carries EAPOL key frames which comply with IEEE Std 802.11-2016, 12.7.2 and
// IEEE Std 802.11-2016, 12.7.6.
pub struct FourwayHandshakeFrame<'a> {
    frame: &'a eapol::KeyFrame,
}

impl<'a> FourwayHandshakeFrame<'a> {
    pub fn from_verified(
        valid_frame: VerifiedKeyFrame<'a>,
        role: Role,
        nonce: Option<&[u8]>,
    ) -> Result<FourwayHandshakeFrame<'a>, failure::Error> {
        // Safe since the frame will be wrapped again in a `KeyFrameState` when being accessed.
        let frame = valid_frame.get().unsafe_get_raw();

        // Drop messages which were not expected by the configured role.
        let msg_no = message_number(frame);
        match role {
            // Authenticator should only receive message 2 and 4.
            Role::Authenticator => match msg_no {
                MessageNumber::Message2 | MessageNumber::Message4 => {}
                _ => bail!(Error::Unexpected4WayHandshakeMessage(msg_no)),
            },
            Role::Supplicant => match msg_no {
                MessageNumber::Message1 | MessageNumber::Message3 => {}
                _ => bail!(Error::Unexpected4WayHandshakeMessage(msg_no)),
            },
        };

        // Explicit validation based on the frame's message number.
        let msg_no = message_number(frame);
        match msg_no {
            MessageNumber::Message1 => validate_message_1(frame),
            MessageNumber::Message2 => validate_message_2(frame),
            MessageNumber::Message3 => validate_message_3(frame, nonce),
            MessageNumber::Message4 => validate_message_4(frame),
        }?;

        Ok(FourwayHandshakeFrame { frame })
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
        let _ = NegotiatedRsne::from_rsne(&s_rsne)?;
        Ok(Config {
            role,
            s_addr,
            s_rsne,
            a_addr,
            a_rsne,
        })
    }
}

#[derive(Debug, PartialEq)]
pub enum Fourway {
    Authenticator(StateMachine<authenticator::State>),
    Supplicant(StateMachine<supplicant::State>),
}

impl Fourway {
    pub fn new(cfg: Config, pmk: Vec<u8>) -> Result<Fourway, failure::Error> {
        let nonce_rdr = NonceReader::new(&cfg.s_addr[..])?;
        let fourway = match &cfg.role {
            Role::Supplicant => {
                let state = supplicant::new(cfg, pmk, nonce_rdr);
                Fourway::Supplicant(StateMachine::new(state))
            },
            Role::Authenticator => {
                let state = authenticator::new(cfg, pmk);
                Fourway::Authenticator(StateMachine::new(state))
            },
        };
        Ok(fourway)
    }
    pub fn initiate(&mut self, update_sink: &mut Vec<SecAssocUpdate>, key_replay_counter: u64)
        -> Result<(), failure::Error>
    {
        match self {
            Fourway::Authenticator(state_machine) => {
                state_machine.replace_state(|state| {
                    // TODO(hahnr): Take anonce from NonceReader.
                    let anonce = vec![0xAc; 32];
                    state.initiate(update_sink, key_replay_counter, anonce)
                });
                Ok(())
            }
            // TODO(hahnr): Supplicant cannot initiate yet.
            _ => Ok(())
        }
    }

    pub fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
        frame: VerifiedKeyFrame,
    ) -> Result<(), failure::Error> {
        match self {
            Fourway::Authenticator(state_machine) => {
                let frame = FourwayHandshakeFrame::from_verified(frame, Role::Authenticator, None)?;
                state_machine.replace_state(|state| {
                    state.on_eapol_key_frame(update_sink, key_replay_counter, frame)
                });
                Ok(())
            }
            Fourway::Supplicant(state_machine) => {
                let anonce = state_machine.state().anonce();
                let frame = FourwayHandshakeFrame::from_verified(frame, Role::Supplicant, anonce)?;
                state_machine.replace_state(|state| {
                    state.on_eapol_key_frame(update_sink, frame)
                });
                Ok(())
            }
        }
    }

    pub fn destroy(self) -> exchange::Config {
        let cfg = match self {
            Fourway::Supplicant(state_machine) => state_machine.into_state().destroy(),
            Fourway::Authenticator(state_machine) => state_machine.into_state().destroy(),
        };
        exchange::Config::FourWayHandshake(cfg)
    }
}

// Verbose and explicit verification of Message 1 to 4 against IEEE Std 802.11-2016, 12.7.6.2.

fn validate_message_1(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    ensure!(
        !frame.key_info.install(),
        Error::InvalidInstallBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(
        frame.key_info.key_ack(),
        Error::InvalidKeyAckBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(
        !frame.key_info.key_mic(),
        Error::InvalidKeyMicBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(
        !frame.key_info.secure(),
        Error::InvalidSecureBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    ensure!(
        !frame.key_info.error(),
        Error::InvalidErrorBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(
        !frame.key_info.request(),
        Error::InvalidRequestBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    ensure!(
        !is_zero(&frame.key_nonce[..]),
        Error::InvalidNonce(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.2
    ensure!(
        is_zero(&frame.key_iv[..]),
        Error::InvalidIv(frame.version, message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    ensure!(frame.key_rsc == 0, Error::InvalidRsc(message_number(frame)));

    // The first message of the Handshake is also required to carry a zeroed MIC.
    // Some routers however send messages without zeroing out the MIC beforehand.
    // To ensure compatibility with such routers, the MIC of the first message is
    // allowed to be set.
    // This assumption faces no security risk because the message's MIC is only
    // validated in the Handshake and not in the Supplicant or Authenticator
    // implementation.
    Ok(())
}

fn validate_message_2(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    ensure!(
        !frame.key_info.install(),
        Error::InvalidInstallBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(
        !frame.key_info.key_ack(),
        Error::InvalidKeyAckBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(
        frame.key_info.key_mic(),
        Error::InvalidKeyMicBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(
        !frame.key_info.secure(),
        Error::InvalidSecureBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    ensure!(
        !frame.key_info.error(),
        Error::InvalidErrorBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(
        !frame.key_info.request(),
        Error::InvalidRequestBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    ensure!(
        !is_zero(&frame.key_nonce[..]),
        Error::InvalidNonce(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.3
    ensure!(
        is_zero(&frame.key_iv[..]),
        Error::InvalidIv(frame.version, message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    ensure!(frame.key_rsc == 0, Error::InvalidRsc(message_number(frame)));

    Ok(())
}

fn validate_message_3(frame: &eapol::KeyFrame, nonce: Option<&[u8]>) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    // Install = 0 is only used in key mapping with TKIP and WEP, neither is supported by Fuchsia.
    ensure!(
        frame.key_info.install(),
        Error::InvalidInstallBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(
        frame.key_info.key_ack(),
        Error::InvalidKeyAckBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(
        frame.key_info.key_mic(),
        Error::InvalidKeyMicBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(
        frame.key_info.secure(),
        Error::InvalidSecureBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    ensure!(
        !frame.key_info.error(),
        Error::InvalidErrorBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(
        !frame.key_info.request(),
        Error::InvalidRequestBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    if let Some(nonce) = nonce {
        ensure!(
            !is_zero(&frame.key_nonce[..]) && &frame.key_nonce[..] == nonce,
            Error::InvalidNonce(message_number(frame))
        );
    }
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.4
    // IEEE 802.11-2016 requires a zeroed IV for 802.1X-2004+ and allows random ones for older
    // protocols. Some APs such as TP-Link violate this requirement and send non-zeroed IVs while
    // using 802.1X-2004. For compatibility, random IVs are allowed for 802.1X-2004.
    ensure!(
        frame.version < eapol::ProtocolVersion::Ieee802dot1x2010 as u8
            || is_zero(&frame.key_iv[..]),
        Error::InvalidIv(frame.version, message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 i) & j)
    // Key Data must not be empty.
    ensure!(
        frame.key_data_len != 0,
        Error::EmptyKeyData(message_number(frame))
    );

    Ok(())
}

fn validate_message_4(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    ensure!(
        !frame.key_info.install(),
        Error::InvalidInstallBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(
        !frame.key_info.key_ack(),
        Error::InvalidKeyAckBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(
        frame.key_info.key_mic(),
        Error::InvalidKeyMicBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(
        frame.key_info.secure(),
        Error::InvalidSecureBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    ensure!(
        !frame.key_info.error(),
        Error::InvalidErrorBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(
        !frame.key_info.request(),
        Error::InvalidRequestBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.5
    ensure!(
        is_zero(&frame.key_iv[..]),
        Error::InvalidIv(frame.version, message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    ensure!(frame.key_rsc == 0, Error::InvalidRsc(message_number(frame)));

    Ok(())
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

fn is_zero(slice: &[u8]) -> bool {
    slice.iter().all(|&x| x == 0)
}

#[cfg(test)]
mod tests {
    use {
        crate::rsna::{
            Role, test_util, UpdateSink, SecAssocUpdate, NegotiatedRsne, VerifiedKeyFrame
        },
        crate::key::{exchange::Key, gtk::Gtk, ptk::Ptk},
    };

    // Create an Authenticator and Supplicant and perfoms the entire 4-Way Handshake.
    #[test]
    fn test_supplicant_with_authenticator() {
        let rsne = NegotiatedRsne::from_rsne(&test_util::get_s_rsne())
            .expect("could not derive negotiated RSNE");

        let mut supplicant = test_util::make_handshake(Role::Supplicant);
        let mut authenticator = test_util::make_handshake(Role::Authenticator);

        // Initiate 4-Way Handshake. The Authenticator will send message #1 of the handshake.
        let mut a_update_sink = vec![];
        let result = authenticator.initiate(&mut a_update_sink, 12);
        assert!(result.is_ok(), "Authenticator failed initiating: {}", result.unwrap_err());
        assert_eq!(a_update_sink.len(), 1);

        // Verify message #1 was sent correctly.
        let msg1 = extract_eapol_resp(&a_update_sink[..])
            .expect("Authenticator did not send msg #1");
        let result = VerifiedKeyFrame::from_key_frame(msg1, &Role::Supplicant, &rsne, 12);
        assert!(result.is_ok(), "failed verifying msg #1 from Authenticator: {}", result.unwrap_err());

        // Supplicant responds with message #2 of the handshake.
        let mut s_update_sink = vec![];
        let result = supplicant.on_eapol_key_frame(&mut s_update_sink, 0, result.unwrap());
        assert!(result.is_ok(), "Supplicant failed processing msg #1: {}", result.unwrap_err());
        assert_eq!(s_update_sink.len(), 2);
        let msg2 = extract_eapol_resp(&s_update_sink[..])
            .expect("Supplicant did not send msg #2");
        let result = VerifiedKeyFrame::from_key_frame(msg2, &Role::Authenticator, &rsne, 12);
        assert!(result.is_ok(), "failed verifying msg #2 from Supplicant: {}", result.unwrap_err());
        let s_ptk = extract_reported_ptk(&s_update_sink[..])
            .expect("Supplicant did not derive PTK").clone();

        // Authenticator responds with message #3 of the handshake.
        let mut a_update_sink = vec![];
        let result = authenticator.on_eapol_key_frame(&mut a_update_sink, 13, result.unwrap());
        assert!(result.is_ok(), "Authenticator failed processing msg #2: {}", result.unwrap_err());
        assert_eq!(a_update_sink.len(), 2);
        let msg3 = extract_eapol_resp(&a_update_sink[..])
            .expect("Authenticator did not send msg #3");
        let result = VerifiedKeyFrame::from_key_frame(msg3, &Role::Supplicant, &rsne, 13);
        assert!(result.is_ok(), "failed verifying msg #3 from Authenticator: {}", result.unwrap_err());
        let a_ptk = extract_reported_ptk(&a_update_sink[..])
            .expect("Authenticator did not derive PTK").clone();

        // Supplicant responds with message #4 of the handshake.
        let mut s_update_sink = vec![];
        let result = supplicant.on_eapol_key_frame(&mut s_update_sink, 0, result.unwrap());
        assert!(result.is_ok(), "Supplicant failed processing msg #3: {}", result.unwrap_err());
        assert_eq!(s_update_sink.len(), 2);
        let msg4 = extract_eapol_resp(&s_update_sink[..])
            .expect("Supplicant did not send msg #4");
        let result = VerifiedKeyFrame::from_key_frame(msg4, &Role::Authenticator, &rsne, 13);
        assert!(result.is_ok(), "failed verifying msg #4 from Supplicant: {}", result.unwrap_err());
        let s_gtk = extract_reported_gtk(&s_update_sink[..])
            .expect("Supplicant did not derive GTK").clone();

        // Authenticator needs to process message #4 of the handshake.
        let mut a_update_sink = vec![];
        let result = authenticator.on_eapol_key_frame(&mut a_update_sink, 0, result.unwrap());
        assert!(result.is_ok(), "Authenticator failed processing msg #4: {}", result.unwrap_err());
        assert_eq!(a_update_sink.len(), 1);
        let a_gtk = extract_reported_gtk(&a_update_sink[..])
            .expect("Authenticator did not derive GTK").clone();

        // Finally verify that Supplicant and Authenticator derived the same keys.
        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    fn extract_eapol_resp(updates: &[SecAssocUpdate]) -> Option<&eapol::KeyFrame> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::TxEapolKeyFrame(resp) => Some(resp),
            _ => None,
        }).next()
    }

    fn extract_reported_ptk(updates: &[SecAssocUpdate]) -> Option<&Ptk> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::Key(Key::Ptk(ptk)) => Some(ptk),
            _ => None,
        }).next()
    }

    fn extract_reported_gtk(updates: &[SecAssocUpdate]) -> Option<&Gtk> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::Key(Key::Gtk(gtk)) => Some(gtk),
            _ => None,
        }).next()
    }

    // First messages of 4-Way Handshake must carry a zeroed IV in all protocol versions.

    #[test]
    fn test_random_iv_msg1_v1() {
        let mut update_sink = UpdateSink::default();
        let (_, msg1_result) = test_util::send_msg1(&mut update_sink, |msg1| {
            msg1.version = 1;
            msg1.key_iv = [0xFFu8; 16];
        });
        assert!(
            msg1_result.is_err(),
            "error, expected failure for first msg but result is: {:?}",
            msg1_result
        );
    }

    #[test]
    fn test_random_iv_msg1_v2() {
        let mut update_sink = UpdateSink::default();
        let (_, msg1_result) = test_util::send_msg1(&mut update_sink, |msg1| {
            msg1.version = 2;
            msg1.key_iv = [0xFFu8; 16];
        });
        assert!(
            msg1_result.is_err(),
            "error, expected failure for first msg but result is: {:?}",
            msg1_result
        );
    }

    // EAPOL Key frames can carry a random IV in the third message of the 4-Way Handshake if
    // protocol version 1, 802.1X-2001, is used. All other protocol versions require a zeroed IV
    // for the third message of the handshake. Some APs violate this requirement, but for
    // compatibility our implementation does in fact allow random IVs with 802.1X-2004.

    #[test]
    fn test_random_iv_msg3_v1() {
        let mut update_sink = UpdateSink::default();
        let (mut env, msg1_result) = test_util::send_msg1(&mut update_sink, |_| {});
        assert!(
            msg1_result.is_ok(),
            "error, expected success for processing first msg but result is: {:?}",
            msg1_result
        );
        let msg3_result = env.send_msg3(&mut update_sink, vec![42u8; 16], |msg3| {
            msg3.version = 1;
            msg3.key_iv = [0xFFu8; 16];
        });
        assert!(
            msg3_result.is_ok(),
            "error, expected success for processing third msg but result is: {:?}",
            msg3_result
        );
    }

    #[test]
    fn test_random_iv_msg3_v2() {
        let mut update_sink = UpdateSink::default();
        let (mut env, msg1_result) = test_util::send_msg1(&mut update_sink, |_| {});
        assert!(
            msg1_result.is_ok(),
            "error, expected success for processing first msg but result is: {:?}",
            msg1_result
        );
        let msg3_result = env.send_msg3(&mut update_sink, vec![42u8; 16], |msg3| {
            msg3.version = 2;
            msg3.key_iv = [0xFFu8; 16];
        });
        assert!(
            msg3_result.is_ok(),
            "error, expected success for processing third msg but result is: {:?}",
            msg3_result
        );
    }

    #[test]
    fn test_zeroed_iv_msg3_v2() {
        let mut update_sink = UpdateSink::default();
        let (mut env, msg1_result) = test_util::send_msg1(&mut update_sink, |_| {});
        assert!(
            msg1_result.is_ok(),
            "error, expected success for processing first msg but result is: {:?}",
            msg1_result
        );
        let msg3_result = env.send_msg3(&mut update_sink, vec![42u8; 16], |msg3| {
            msg3.version = 2;
            msg3.key_iv = [0u8; 16];
        });
        assert!(
            msg3_result.is_ok(),
            "error, expected success for processing third msg but result is: {:?}",
            msg3_result
        );
    }

    #[test]
    fn test_random_iv_msg3_v3() {
        let mut update_sink = UpdateSink::default();
        let (mut env, msg1_result) = test_util::send_msg1(&mut update_sink, |_| {});
        assert!(
            msg1_result.is_ok(),
            "error, expected success for processing first msg but result is: {:?}",
            msg1_result
        );
        let msg3_result = env.send_msg3(&mut update_sink, vec![42u8; 16], |msg3| {
            msg3.version = 3;
            msg3.key_iv = [0xFFu8; 16];
        });
        // Random IVs are not allowed for v2 but because some APs violate this requirement, we have
        // to allow such IVs.
        assert!(
            msg3_result.is_err(),
            "error, expected failure for third msg but result is: {:?}",
            msg3_result
        );
    }
}
