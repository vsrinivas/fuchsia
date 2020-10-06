// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod authenticator;
mod supplicant;

use crate::crypto_utils::nonce::NonceReader;
use crate::key::{exchange, gtk::GtkProvider};
use crate::rsna::{Dot11VerifiedKeyFrame, NegotiatedProtection, Role, SecAssocUpdate, UpdateSink};
use crate::ProtectionInfo;
use crate::{rsn_ensure, Error};
use eapol;
use std::sync::{Arc, Mutex};
use wlan_statemachine::StateMachine;
use zerocopy::ByteSlice;

#[derive(Debug, PartialEq)]
pub enum MessageNumber {
    Message1 = 1,
    Message2 = 2,
    Message3 = 3,
    Message4 = 4,
}

/// Struct which carries EAPOL key frames which comply with IEEE Std 802.11-2016, 12.7.2 and
/// IEEE Std 802.11-2016, 12.7.6.
pub struct FourwayHandshakeFrame<B: ByteSlice>(Dot11VerifiedKeyFrame<B>);

impl<B: ByteSlice> FourwayHandshakeFrame<B> {
    pub fn from_verified(
        frame: Dot11VerifiedKeyFrame<B>,
        role: Role,
        nonce: Option<&[u8]>,
    ) -> Result<FourwayHandshakeFrame<B>, Error> {
        // Safe: the raw frame is never exposed outside of this function.
        let raw_frame = frame.unsafe_get_raw();
        // Drop messages which were not expected by the configured role.
        let msg_no = message_number(raw_frame);
        match role {
            // Authenticator should only receive message 2 and 4.
            Role::Authenticator => match msg_no {
                MessageNumber::Message2 | MessageNumber::Message4 => {}
                _ => return Err(Error::UnexpectedHandshakeMessage(msg_no.into()).into()),
            },
            Role::Supplicant => match msg_no {
                MessageNumber::Message1 | MessageNumber::Message3 => {}
                _ => return Err(Error::UnexpectedHandshakeMessage(msg_no.into()).into()),
            },
        };

        // Explicit validation based on the frame's message number.
        match msg_no {
            MessageNumber::Message1 => validate_message_1(raw_frame),
            MessageNumber::Message2 => validate_message_2(raw_frame),
            MessageNumber::Message3 => validate_message_3(raw_frame, nonce),
            MessageNumber::Message4 => validate_message_4(raw_frame),
        }?;

        Ok(FourwayHandshakeFrame(frame))
    }

    pub fn get(self) -> Dot11VerifiedKeyFrame<B> {
        self.0
    }

    /// Returns the 4-Way Handshake's message number.
    fn message_number(&self) -> MessageNumber {
        // Safe: At this point the frame was validated to be a valid 4-Way Handshake frame.
        message_number(self.unsafe_get_raw())
    }
}

impl<B: ByteSlice> std::ops::Deref for FourwayHandshakeFrame<B> {
    type Target = Dot11VerifiedKeyFrame<B>;

    fn deref(&self) -> &Dot11VerifiedKeyFrame<B> {
        &self.0
    }
}

#[derive(Debug, Clone)]
pub struct Config {
    pub role: Role,
    pub s_addr: [u8; 6],
    pub s_protection: ProtectionInfo,
    pub a_addr: [u8; 6],
    pub a_protection: ProtectionInfo,
    pub nonce_rdr: Arc<NonceReader>,
    pub gtk_provider: Option<Arc<Mutex<GtkProvider>>>,
}

impl Config {
    pub fn new(
        role: Role,
        s_addr: [u8; 6],
        s_protection: ProtectionInfo,
        a_addr: [u8; 6],
        a_protection: ProtectionInfo,
        nonce_rdr: Arc<NonceReader>,
        gtk_provider: Option<Arc<Mutex<GtkProvider>>>,
    ) -> Result<Config, Error> {
        rsn_ensure!(
            role != Role::Authenticator || gtk_provider.is_some(),
            "GtkProvider is missing"
        );
        rsn_ensure!(
            NegotiatedProtection::from_protection(&s_protection).is_ok(),
            "invalid supplicant protection"
        );

        Ok(Config { role, s_addr, s_protection, a_addr, a_protection, nonce_rdr, gtk_provider })
    }
}

impl PartialEq for Config {
    fn eq(&self, other: &Config) -> bool {
        self.role == other.role
            && self.s_addr == other.s_addr
            && self.s_protection == other.s_protection
            && self.a_addr == other.a_addr
            && self.a_protection == other.a_protection
    }
}

#[derive(Debug, PartialEq)]
pub enum Fourway {
    Authenticator(StateMachine<authenticator::State>),
    Supplicant(StateMachine<supplicant::State>),
}

impl Fourway {
    pub fn new(cfg: Config, pmk: Vec<u8>) -> Result<Fourway, anyhow::Error> {
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
    ) -> Result<(), Error> {
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

    pub fn on_eapol_key_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
        frame: Dot11VerifiedKeyFrame<B>,
    ) -> Result<(), Error> {
        match self {
            Fourway::Authenticator(state_machine) => {
                let frame = FourwayHandshakeFrame::from_verified(frame, Role::Authenticator, None)?;
                state_machine.replace_state(|state| {
                    state.on_eapol_key_frame(update_sink, key_replay_counter, frame)
                });
                Ok(())
            }
            Fourway::Supplicant(state_machine) => {
                let anonce = state_machine.as_ref().anonce();
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

fn validate_message_1<B: ByteSlice>(frame: &eapol::KeyFrameRx<B>) -> Result<(), Error> {
    let key_info = frame.key_frame_fields.key_info();
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    rsn_ensure!(!key_info.install(), Error::InvalidInstallBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    rsn_ensure!(key_info.key_ack(), Error::InvalidKeyAckBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    rsn_ensure!(!key_info.key_mic(), Error::InvalidKeyMicBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    rsn_ensure!(!key_info.secure(), Error::InvalidSecureBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    rsn_ensure!(!key_info.error(), Error::InvalidErrorBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    rsn_ensure!(!key_info.request(), Error::InvalidRequestBitValue(MessageNumber::Message1.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    rsn_ensure!(
        !key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(MessageNumber::Message1.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    rsn_ensure!(
        !is_zero(&frame.key_frame_fields.key_nonce[..]),
        Error::InvalidNonce(MessageNumber::Message1.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.2
    rsn_ensure!(
        is_zero(&frame.key_frame_fields.key_iv[..]),
        Error::InvalidIv(frame.eapol_fields.version, MessageNumber::Message1.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    rsn_ensure!(
        frame.key_frame_fields.key_rsc.to_native() == 0,
        Error::InvalidRsc(MessageNumber::Message1.into())
    );

    // The first message of the Handshake is also required to carry a zeroed MIC.
    // Some routers however send messages without zeroing out the MIC beforehand.
    // To ensure compatibility with such routers, the MIC of the first message is
    // allowed to be set.
    // This assumption faces no security risk because the message's MIC is only
    // validated in the Handshake and not in the Supplicant or Authenticator
    // implementation.
    Ok(())
}

fn validate_message_2<B: ByteSlice>(frame: &eapol::KeyFrameRx<B>) -> Result<(), Error> {
    let key_info = frame.key_frame_fields.key_info();
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    rsn_ensure!(!key_info.install(), Error::InvalidInstallBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    rsn_ensure!(!key_info.key_ack(), Error::InvalidKeyAckBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    rsn_ensure!(key_info.key_mic(), Error::InvalidKeyMicBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    rsn_ensure!(!key_info.secure(), Error::InvalidSecureBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    rsn_ensure!(!key_info.error(), Error::InvalidErrorBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    rsn_ensure!(!key_info.request(), Error::InvalidRequestBitValue(MessageNumber::Message2.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    rsn_ensure!(
        !key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(MessageNumber::Message2.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 e)
    rsn_ensure!(
        !is_zero(&frame.key_frame_fields.key_nonce[..]),
        Error::InvalidNonce(MessageNumber::Message2.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.3
    rsn_ensure!(
        is_zero(&frame.key_frame_fields.key_iv[..]),
        Error::InvalidIv(frame.eapol_fields.version, MessageNumber::Message2.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    rsn_ensure!(
        frame.key_frame_fields.key_rsc.to_native() == 0,
        Error::InvalidRsc(MessageNumber::Message2.into())
    );

    Ok(())
}

fn validate_message_3<B: ByteSlice>(
    frame: &eapol::KeyFrameRx<B>,
    nonce: Option<&[u8]>,
) -> Result<(), Error> {
    let key_info = frame.key_frame_fields.key_info();
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    // Install = 0 is only used in key mapping with TKIP and WEP, neither is supported by Fuchsia.
    rsn_ensure!(key_info.install(), Error::InvalidInstallBitValue(MessageNumber::Message3.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    rsn_ensure!(key_info.key_ack(), Error::InvalidKeyAckBitValue(MessageNumber::Message3.into()));
    // These bit values are not used by WPA1.
    if frame.key_frame_fields.descriptor_type != eapol::KeyDescriptor::LEGACY_WPA1 {
        // IEEE Std 802.11-2016, 12.7.2 b.6)
        rsn_ensure!(
            key_info.key_mic(),
            Error::InvalidKeyMicBitValue(MessageNumber::Message3.into())
        );
        // IEEE Std 802.11-2016, 12.7.2 b.7)
        rsn_ensure!(
            key_info.secure(),
            Error::InvalidSecureBitValue(MessageNumber::Message3.into())
        );
        // IEEE Std 802.11-2016, 12.7.2 b.10)
        rsn_ensure!(
            key_info.encrypted_key_data(),
            Error::InvalidEncryptedKeyDataBitValue(MessageNumber::Message3.into())
        );
    }
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    rsn_ensure!(!key_info.error(), Error::InvalidErrorBitValue(MessageNumber::Message3.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    rsn_ensure!(!key_info.request(), Error::InvalidRequestBitValue(MessageNumber::Message3.into()));
    // IEEE Std 802.11-2016, 12.7.2 e)
    if let Some(nonce) = nonce {
        rsn_ensure!(
            !is_zero(&frame.key_frame_fields.key_nonce[..])
                && &frame.key_frame_fields.key_nonce[..] == nonce,
            Error::InvalidNonce(MessageNumber::Message3.into())
        );
    }
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.4
    // IEEE 802.11-2016 requires a zeroed IV for 802.1X-2004+ and allows random ones for older
    // protocols. Some APs such as TP-Link violate this requirement and send non-zeroed IVs while
    // using 802.1X-2004. For compatibility, random IVs are allowed for 802.1X-2004.
    rsn_ensure!(
        frame.eapol_fields.version < eapol::ProtocolVersion::IEEE802DOT1X2010
            || is_zero(&frame.key_frame_fields.key_iv[..]),
        Error::InvalidIv(frame.eapol_fields.version, MessageNumber::Message3.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 i) & j)
    // Key Data must not be empty.
    rsn_ensure!(frame.key_data.len() != 0, Error::EmptyKeyData(MessageNumber::Message3.into()));

    Ok(())
}

fn validate_message_4<B: ByteSlice>(frame: &eapol::KeyFrameRx<B>) -> Result<(), Error> {
    let key_info = frame.key_frame_fields.key_info();
    // IEEE Std 802.11-2016, 12.7.2 b.4)
    rsn_ensure!(!key_info.install(), Error::InvalidInstallBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.5)
    rsn_ensure!(!key_info.key_ack(), Error::InvalidKeyAckBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.6)
    rsn_ensure!(key_info.key_mic(), Error::InvalidKeyMicBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.7)
    rsn_ensure!(key_info.secure(), Error::InvalidSecureBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.8)
    // Error bit only set by Supplicant in MIC failures in SMK derivation.
    // SMK derivation not yet supported.
    rsn_ensure!(!key_info.error(), Error::InvalidErrorBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.9)
    rsn_ensure!(!key_info.request(), Error::InvalidRequestBitValue(MessageNumber::Message4.into()));
    // IEEE Std 802.11-2016, 12.7.2 b.10)
    rsn_ensure!(
        !key_info.encrypted_key_data(),
        Error::InvalidEncryptedKeyDataBitValue(MessageNumber::Message4.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 f)
    // IEEE Std 802.11-2016, 12.7.6.5
    rsn_ensure!(
        is_zero(&frame.key_frame_fields.key_iv[..]),
        Error::InvalidIv(frame.eapol_fields.version, MessageNumber::Message4.into())
    );
    // IEEE Std 802.11-2016, 12.7.2 g)
    rsn_ensure!(
        frame.key_frame_fields.key_rsc.to_native() == 0,
        Error::InvalidRsc(MessageNumber::Message4.into())
    );

    Ok(())
}

fn message_number<B: ByteSlice>(rx_frame: &eapol::KeyFrameRx<B>) -> MessageNumber {
    // IEEE does not specify how to determine a frame's message number in the 4-Way Handshake
    // sequence. However, it's important to know a frame's message number to do further
    // validations. To derive the message number the key info field is used.
    // 4-Way Handshake specific EAPOL Key frame requirements:
    // IEEE Std 802.11-2016, 12.7.6.1

    // IEEE Std 802.11-2016, 12.7.6.2 & 12.7.6.4
    // Authenticator requires acknowledgement of all its sent frames.
    if rx_frame.key_frame_fields.key_info().key_ack() {
        // Authenticator only sends 1st and 3rd message of the handshake.
        // IEEE Std 802.11-2016, 12.7.2 b.4)
        // The third requires key installation while the first one doesn't.
        if rx_frame.key_frame_fields.key_info().install() {
            MessageNumber::Message3
        } else {
            MessageNumber::Message1
        }
    } else {
        // Supplicant only sends 2nd and 4th message of the handshake.
        // IEEE Std 802.11-2016, 12.7.2 b.7)
        // The fourth message is secured while the second one is not.
        if rx_frame.key_frame_fields.key_info().secure() {
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
    use super::Fourway;
    use crate::key::ptk::Ptk;
    use crate::rsna::{test_util, SecAssocUpdate, UpdateSink};
    use wlan_common::big_endian::BigEndianU64;

    // Create an Authenticator and Supplicant and performs the entire 4-Way Handshake.
    #[test]
    fn test_supplicant_with_authenticator() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        assert_eq!(msg1.keyframe().eapol_fields.version, eapol::ProtocolVersion::IEEE802DOT1X2004);
        let (msg2, _) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        assert_eq!(msg2.keyframe().eapol_fields.version, eapol::ProtocolVersion::IEEE802DOT1X2004);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        assert_eq!(msg3.keyframe().eapol_fields.version, eapol::ProtocolVersion::IEEE802DOT1X2004);
        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3.keyframe(), 13);
        assert_eq!(msg4.keyframe().eapol_fields.version, eapol::ProtocolVersion::IEEE802DOT1X2004);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        // Finally verify that Supplicant and Authenticator derived the same keys.
        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    fn run_wpa3_handshake(gtk: &[u8], igtk: Option<&[u8]>) -> (Ptk, Vec<SecAssocUpdate>) {
        let anonce = [0xab; 32];
        let mut supplicant = test_util::make_wpa3_handshake(super::Role::Supplicant);
        let protection = test_util::get_wpa3_protection();
        let msg1_buf = test_util::get_wpa3_4whs_msg1(&anonce[..]);
        let msg1 = msg1_buf.keyframe();
        let updates = test_util::send_msg_to_fourway(&mut supplicant, msg1, 0, &protection);
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let a_ptk =
            test_util::get_wpa3_ptk(&anonce[..], &msg2.keyframe().key_frame_fields.key_nonce[..]);
        let msg3_buf = &test_util::get_wpa3_4whs_msg3(&a_ptk, &anonce[..], &gtk[..], igtk);
        let msg3 = msg3_buf.keyframe();
        (a_ptk, test_util::send_msg_to_fourway(&mut supplicant, msg3, 0, &protection))
    }

    #[test]
    fn test_wpa3_handshake_generates_igtk() {
        let gtk = [0xbb; 32];
        let igtk = [0xcc; 32];
        let (ptk, updates) = run_wpa3_handshake(&gtk[..], Some(&igtk[..]));

        test_util::expect_eapol_resp(&updates[..]);
        let s_ptk = test_util::expect_reported_ptk(&updates[..]);
        let s_gtk = test_util::expect_reported_gtk(&updates[..]);
        let s_igtk = test_util::expect_reported_igtk(&updates[..]);
        assert_eq!(s_ptk, ptk);
        assert_eq!(&s_gtk.gtk[..], &gtk[..]);
        assert_eq!(&s_igtk.igtk[..], &igtk[..]);
    }

    #[test]
    fn test_wpa3_handshake_requires_igtk() {
        let gtk = [0xbb; 32];
        let (_ptk, updates) = run_wpa3_handshake(&gtk[..], None);
        assert!(
            test_util::get_eapol_resp(&updates[..]).is_none(),
            "WPA3 should not send EAPOL msg4 without IGTK"
        );
    }

    #[test]
    fn test_wpa1_handshake() {
        let pmk = test_util::get_pmk();
        let cfg = test_util::make_wpa1_fourway_cfg();
        let protection = test_util::get_wpa1_protection();
        let mut supplicant = Fourway::new(cfg, pmk).expect("error while creating 4-Way Handshake");

        // We don't have a WPA1 authenticator so we use fake messages.
        let anonce = [0xab; 32];
        let msg1_buf = test_util::get_wpa1_4whs_msg1(&anonce[..]);
        let msg1 = msg1_buf.keyframe();
        let updates = test_util::send_msg_to_fourway(&mut supplicant, msg1, 0, &protection);
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let a_ptk =
            test_util::get_wpa1_ptk(&anonce[..], &msg2.keyframe().key_frame_fields.key_nonce[..]);
        let msg3_buf = &test_util::get_wpa1_4whs_msg3(&a_ptk, &anonce[..]);
        let msg3 = msg3_buf.keyframe();
        let updates = test_util::send_msg_to_fourway(&mut supplicant, msg3, 0, &protection);

        // Verify that we completed the exchange and computed the same PTK as our fake AP would.
        test_util::expect_eapol_resp(&updates[..]);
        let s_ptk = test_util::expect_reported_ptk(&updates[..]);
        assert_eq!(s_ptk, a_ptk);
    }

    #[test]
    fn test_supplicant_replay_msg3() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let (msg2, _) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let (_, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3.keyframe(), 13);

        // Replay third message pretending Authenticator did not receive Supplicant's response.
        let mut update_sink = UpdateSink::default();

        env.send_msg3_to_supplicant_capture_updates(msg3.keyframe(), 13, &mut update_sink);
        let msg4 = test_util::expect_eapol_resp(&update_sink[..]);

        for update in update_sink {
            if let SecAssocUpdate::Key(_) = update {
                panic!("reinstalled key");
            }
        }

        // Let Authenticator process 4th message.
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        // Finally verify that Supplicant and Authenticator derived the same keys.
        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_supplicant_replay_msg3_different_gtk() {
        let mut env = test_util::FourwayTestEnv::new();

        // Use arbitrarily chosen key_replay_counter.
        let msg1 = env.initiate(12);
        let anonce = msg1.keyframe().key_frame_fields.key_nonce.clone();
        let (msg2, _) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let (_, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3.keyframe(), 13);

        // Replay third message pretending Authenticator did not receive Supplicant's response.
        // Modify GTK to simulate GTK rotation while 4-Way Handshake was in progress.
        let mut other_gtk = s_gtk.gtk.clone();
        other_gtk[0] ^= 0xFF;
        let msg3 = test_util::get_wpa2_4whs_msg3(&s_ptk, &anonce[..], &other_gtk[..], |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(42);
        });
        let mut update_sink = UpdateSink::default();
        env.send_msg3_to_supplicant_capture_updates(msg3.keyframe(), 13, &mut update_sink);

        // Ensure Supplicant rejected and dropped 3rd message without replying.
        assert_eq!(update_sink.len(), 0);
    }

    // First messages of 4-Way Handshake must carry a zeroed IV in all protocol versions.

    #[test]
    fn test_random_iv_msg1_v1() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(1);
        let mut buf = vec![];
        let mut msg1 = msg1.copy_keyframe_mut(&mut buf);
        msg1.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2001;
        msg1.key_frame_fields.key_iv = [0xFFu8; 16];
        env.send_msg1_to_supplicant_expect_err(msg1, 1);
    }

    #[test]
    fn test_random_iv_msg1_v2() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(1);
        let mut buf = vec![];
        let mut msg1 = msg1.copy_keyframe_mut(&mut buf);
        msg1.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2004;
        msg1.key_frame_fields.key_iv = [0xFFu8; 16];
        env.send_msg1_to_supplicant_expect_err(msg1, 1);
    }

    // EAPOL Key frames can carry a random IV in the third message of the 4-Way Handshake if
    // protocol version 1, 802.1X-2001, is used. All other protocol versions require a zeroed IV
    // for the third message of the handshake. Some vendors violate this requirement. For
    // compatibility, Fuchsia relaxes this requirement and allows random IVs with 802.1X-2004.

    #[test]
    fn test_random_iv_msg3_v2001() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let mut buf = vec![];
        let mut msg3 = msg3.copy_keyframe_mut(&mut buf);
        msg3.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2001;
        msg3.key_frame_fields.key_iv = [0xFFu8; 16];
        test_util::finalize_key_frame(&mut msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_random_iv_msg3_v2004() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let mut buf = vec![];
        let mut msg3 = msg3.copy_keyframe_mut(&mut buf);
        msg3.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2004;
        msg3.key_frame_fields.key_iv = [0xFFu8; 16];
        test_util::finalize_key_frame(&mut msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_zeroed_iv_msg3_v2004() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let mut buf = vec![];
        let mut msg3 = msg3.copy_keyframe_mut(&mut buf);
        msg3.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2004;
        msg3.key_frame_fields.key_iv = [0u8; 16];
        test_util::finalize_key_frame(&mut msg3, Some(ptk.kck()));

        let (msg4, s_ptk, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        let (a_ptk, a_gtk) = env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        assert_eq!(s_ptk, a_ptk);
        assert_eq!(s_gtk, a_gtk);
    }

    #[test]
    fn test_random_iv_msg3_v2010() {
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let mut buf = vec![];
        let mut msg3 = msg3.copy_keyframe_mut(&mut buf);
        msg3.eapol_fields.version = eapol::ProtocolVersion::IEEE802DOT1X2010;
        msg3.key_frame_fields.key_iv = [0xFFu8; 16];
        test_util::finalize_key_frame(&mut msg3, Some(ptk.kck()));

        env.send_msg3_to_supplicant_expect_err(msg3, 13);
    }

    #[test]
    fn derive_correct_gtk_rsc() {
        const GTK_RSC: u64 = 981234;
        let mut env = test_util::FourwayTestEnv::new();

        let msg1 = env.initiate(12);
        let (msg2, ptk) = env.send_msg1_to_supplicant(msg1.keyframe(), 12);
        let msg3 = env.send_msg2_to_authenticator(msg2.keyframe(), 12, 13);
        let mut buf = vec![];
        let mut msg3 = msg3.copy_keyframe_mut(&mut buf);
        msg3.key_frame_fields.key_rsc = BigEndianU64::from_native(GTK_RSC);
        test_util::finalize_key_frame(&mut msg3, Some(ptk.kck()));

        let (msg4, _, s_gtk) = env.send_msg3_to_supplicant(msg3, 13);
        env.send_msg4_to_authenticator(msg4.keyframe(), 13);

        // Verify Supplicant picked up the Authenticator's GTK RSC.
        assert_eq!(s_gtk.rsc, GTK_RSC);
    }
}
