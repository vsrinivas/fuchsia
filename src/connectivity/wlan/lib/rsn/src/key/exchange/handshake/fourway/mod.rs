// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod authenticator;
mod supplicant;

use crate::crypto_utils::nonce::NonceReader;
use crate::key::{exchange, gtk::GtkProvider};
use crate::rsna::{
    KeyFrameKeyDataState, KeyFrameState, NegotiatedRsne, Role, SecAssocUpdate, UpdateSink,
    VerifiedKeyFrame,
};
use crate::state_machine::StateMachine;
use crate::Error;
use eapol;
use failure::{self, bail, ensure};
use std::sync::{Arc, Mutex};
use wlan_common::ie::rsn::rsne::Rsne;

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

#[derive(Debug, Clone)]
pub struct Config {
    pub role: Role,
    pub s_addr: [u8; 6],
    pub s_rsne: Rsne,
    pub a_addr: [u8; 6],
    pub a_rsne: Rsne,
    pub nonce_rdr: Arc<NonceReader>,
    pub gtk_provider: Option<Arc<Mutex<GtkProvider>>>,
}

impl Config {
    pub fn new(
        role: Role,
        s_addr: [u8; 6],
        s_rsne: Rsne,
        a_addr: [u8; 6],
        a_rsne: Rsne,
        nonce_rdr: Arc<NonceReader>,
        gtk_provider: Option<Arc<Mutex<GtkProvider>>>,
    ) -> Result<Config, failure::Error> {
        ensure!(role != Role::Authenticator || gtk_provider.is_some(), "GtkProvider is missing");
        ensure!(NegotiatedRsne::from_rsne(&s_rsne).is_ok(), "invalid s_rsne");

        Ok(Config { role, s_addr, s_rsne, a_addr, a_rsne, nonce_rdr, gtk_provider })
    }
}

impl PartialEq for Config {
    fn eq(&self, other: &Config) -> bool {
        self.role == other.role
            && self.s_addr == other.s_addr
            && self.s_rsne == other.s_rsne
            && self.a_addr == other.a_addr
            && self.a_rsne == other.a_rsne
    }
}

#[derive(Debug, PartialEq)]
pub enum Fourway {
    Authenticator(StateMachine<authenticator::State>),
    Supplicant(StateMachine<supplicant::State>),
}

impl Fourway {
    pub fn new(cfg: Config, pmk: Vec<u8>) -> Result<Fourway, failure::Error> {
        let fourway = match &cfg.role {
            Role::Supplicant => {
                let state = supplicant::new(cfg, pmk);
                Fourway::Supplicant(StateMachine::new(state))
            }
            Role::Authenticator => {
                let state = authenticator::new(cfg, pmk);
                Fourway::Authenticator(StateMachine::new(state))
            }
        };
        Ok(fourway)
    }

    pub fn initiate(
        &mut self,
        update_sink: &mut Vec<SecAssocUpdate>,
        key_replay_counter: u64,
    ) -> Result<(), failure::Error> {
        match self {
            Fourway::Authenticator(state_machine) => {
                state_machine
                    .replace_state(|state| state.initiate(update_sink, key_replay_counter));
                Ok(())
            }
            // TODO(hahnr): Supplicant cannot initiate yet.
            _ => Ok(()),
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
                state_machine.replace_state(|state| state.on_eapol_key_frame(update_sink, frame));
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
    ensure!(!frame.key_info.install(), Error::InvalidInstallBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(frame.key_info.key_ack(), Error::InvalidKeyAckBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(!frame.key_info.key_mic(), Error::InvalidKeyMicBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(!frame.key_info.secure(), Error::InvalidSecureBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    ensure!(!frame.key_info.error(), Error::InvalidErrorBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(!frame.key_info.request(), Error::InvalidRequestBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    ensure!(!is_zero(&frame.key_nonce[..]), Error::InvalidNonce(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.2
    ensure!(is_zero(&frame.key_iv[..]), Error::InvalidIv(frame.version, message_number(frame)));
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
    ensure!(!frame.key_info.install(), Error::InvalidInstallBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(!frame.key_info.key_ack(), Error::InvalidKeyAckBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(frame.key_info.key_mic(), Error::InvalidKeyMicBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(!frame.key_info.secure(), Error::InvalidSecureBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    ensure!(!frame.key_info.error(), Error::InvalidErrorBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(!frame.key_info.request(), Error::InvalidRequestBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    ensure!(!is_zero(&frame.key_nonce[..]), Error::InvalidNonce(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.3
    ensure!(is_zero(&frame.key_iv[..]), Error::InvalidIv(frame.version, message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 g)
    ensure!(frame.key_rsc == 0, Error::InvalidRsc(message_number(frame)));

    Ok(())
}

fn validate_message_3(frame: &eapol::KeyFrame, nonce: Option<&[u8]>) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    // Install = 0 is only used in key mapping with TKIP and WEP, neither is supported by Fuchsia.
    ensure!(frame.key_info.install(), Error::InvalidInstallBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(frame.key_info.key_ack(), Error::InvalidKeyAckBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(frame.key_info.key_mic(), Error::InvalidKeyMicBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(frame.key_info.secure(), Error::InvalidSecureBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    ensure!(!frame.key_info.error(), Error::InvalidErrorBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(!frame.key_info.request(), Error::InvalidRequestBitValue(message_number(frame)));
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
    ensure!(frame.key_data_len != 0, Error::EmptyKeyData(message_number(frame)));

    Ok(())
}

fn validate_message_4(frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    ensure!(!frame.key_info.install(), Error::InvalidInstallBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    ensure!(!frame.key_info.key_ack(), Error::InvalidKeyAckBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    ensure!(frame.key_info.key_mic(), Error::InvalidKeyMicBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    ensure!(frame.key_info.secure(), Error::InvalidSecureBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    ensure!(!frame.key_info.error(), Error::InvalidErrorBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    ensure!(!frame.key_info.request(), Error::InvalidRequestBitValue(message_number(frame)));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    ensure!(
        !frame.key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(message_number(frame))
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.5
    ensure!(is_zero(&frame.key_iv[..]), Error::InvalidIv(frame.version, message_number(frame)));
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
    use crate::rsna::{test_util, UpdateSink};

    // Create an Authenticator and Supplicant and perfoms the entire 4-Way Handshake.
    #[test]
    fn test_supplicant_with_authenticator() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        assert_eq!(msg1.version, eapol::ProtocolVersion::Ieee802dot1x2004 as u8);
        let (msg2, _) = env.send_msg1_to_supplicant(msg1, 12);
        assert_eq!(msg2.version, eapol::ProtocolVersion::Ieee802dot1x2004 as u8);
        let msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        assert_eq!(msg3.version, eapol::ProtocolVersion::Ieee802dot1x2004 as u8);
        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        assert_eq!(msg4.version, eapol::ProtocolVersion::Ieee802dot1x2004 as u8);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4, 13);

        // Finally verify that Supplicant and Authenticator derived the same keys.
        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_supplicant_replay_msg3() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let (msg2, _) = env.send_msg1_to_supplicant(msg1, 12);
        let msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        let (_, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3.clone(), 13);

        // Replay third message pretending Authenticator did not receive Supplicant's response.
        let mut update_sink = UpdateSink::default();
        env.send_msg3_to_supplicant_capture_updates(msg3, 13, &mut update_sink);
        let msg4 = test_util::expect_eapol_resp(&update_sink[..]);
        assert!(test_util::get_reported_ptk(&update_sink).is_none(), "reinstalled PTK");
        assert!(test_util::get_reported_gtk(&update_sink).is_none(), "reinstalled GTK");

        // Let Authenticator process 4th message.
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4, 13);

        // Finally verify that Supplicant and Authenticator derived the same keys.
        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_supplicant_replay_msg3_different_gtk() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let anonce = msg1.key_nonce.clone();
        let (msg2, _) = env.send_msg1_to_supplicant(msg1, 12);
        let msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        let (_, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3.clone(), 13);

        // Replay third message pretending Authenticator did not receive Supplicant's response.
        // Modify GTK to simulate GTK rotation while 4-Way Handshake was in progress.
        let mut other_gtk = s_gtk.gtk.clone();
        other_gtk[0] ^= 0xFF;
        let msg3 = test_util::get_4whs_msg3(&s_ptk, &anonce[..], &other_gtk[..], |msg3| {
            msg3.key_replay_counter = 42;
        });
        let mut update_sink = UpdateSink::default();
        env.send_msg3_to_supplicant_capture_updates(msg3, 13, &mut update_sink);

        // Ensure Supplicant rejected and dropped 3rd message without replying.
        assert_eq!(update_sink.len(), 0);
    }

    // First messages of 4-Way Handshake must carry a zeroed IV in all protocol versions.

    #[test]
    fn test_random_iv_msg1_v1() {
        let mut env = test_util::FourwayTestEnv::new();

        let mut msg1 = env.initiate(1);
        msg1.version = 1;
        msg1.key_iv = [0xFFu8; 16];
        env.send_msg1_to_supplicant_expect_err(msg1, 1);
    }

    #[test]
    fn test_random_iv_msg1_v2() {
        let mut env = test_util::FourwayTestEnv::new();

        let mut msg1 = env.initiate(1);
        msg1.version = 2;
        msg1.key_iv = [0xFFu8; 16];
        env.send_msg1_to_supplicant_expect_err(msg1, 1);
    }

    // EAPOL Key frames can carry a random IV in the third message of the 4-Way Handshake if
    // protocol version 1, 802.1X-2001, is used. All other protocol versions require a zeroed IV
    // for the third message of the handshake. Some vendors violate this requirement. For
    // compatibility, Fuchsia relaxes this requirement and allows random IVs with 802.1X-2004.

    #[test]
    fn test_random_iv_msg3_v1() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        let mut msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        msg3.version = 1;
        msg3.key_iv = [0xFFu8; 16];
        msg3 = test_util::finalize_key_frame(msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4, 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_random_iv_msg3_v2() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        let mut msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        msg3.version = 2;
        msg3.key_iv = [0xFFu8; 16];
        msg3 = test_util::finalize_key_frame(msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4, 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_zeroed_iv_msg3_v2() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        let mut msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        msg3.version = 2;
        msg3.key_iv = [0u8; 16];
        msg3 = test_util::finalize_key_frame(msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4, 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_random_iv_msg3_v3() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        let mut msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        msg3.version = 3;
        msg3.key_iv = [0xFFu8; 16];
        msg3 = test_util::finalize_key_frame(msg3, Some(ptk.kck()));

        env.send_msg3_to_supplicant_expect_err(msg3, 13);
    }

    #[test]
    fn derive_correct_gtk_rsc() {
        const GTK_RSC: u64 = 981234;
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1, 12);
        let mut msg3 = env.send_msg2_to_authenticator(msg2, 12, 13);
        msg3.key_rsc = GTK_RSC;
        msg3 = test_util::finalize_key_frame(msg3, Some(ptk.kck()));

        let (msg4, _, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        env.send_msg4_to_authenticator(msg4, 13);

        // Verify Supplicant picked up the Authenticator's GTK RSC.
        assert_eq!(s_gtk.rsc, GTK_RSC);
    }
}
