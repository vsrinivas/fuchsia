// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_utils::nonce::Nonce;
use crate::key::exchange::handshake::fourway::{self, Config, FourwayHandshakeFrame};
use crate::key::exchange::{compute_mic, Key};
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::key_data;
use crate::rsna::{
    KeyFrameKeyDataState, KeyFrameState, NegotiatedRsne, SecAssocUpdate, UpdateSink,
};
use crate::Error;
use bytes::Bytes;
use crypto::util::fixed_time_eq;
use eapol;
use failure::{self, bail, ensure};
use log::error;
use wlan_common::ie::rsn::rsne::Rsne;

// IEEE Std 802.11-2016, 12.7.6.2
fn handle_message_1(
    cfg: &Config,
    pmk: &[u8],
    snonce: &[u8],
    msg1: FourwayHandshakeFrame,
) -> Result<(eapol::KeyFrame, Ptk, Nonce), failure::Error> {
    let frame = match msg1.get() {
        // Note: This is only true if PTK re-keying is not supported.
        KeyFrameState::UnverifiedMic(_) => bail!("msg1 of 4-Way Handshake cannot carry a MIC"),
        KeyFrameState::NoMic(frame) => frame,
    };
    let anonce = frame.key_nonce;
    let rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;

    let pairwise = rsne.pairwise.clone();
    let ptk = Ptk::new(pmk, &cfg.a_addr, &cfg.s_addr, &anonce[..], snonce, &rsne.akm, pairwise)?;
    let msg2 = create_message_2(cfg, ptk.kck(), &rsne, frame, &snonce[..])?;

    Ok((msg2, ptk, anonce))
}

// IEEE Std 802.11-2016, 12.7.6.3
fn create_message_2(
    cfg: &Config,
    kck: &[u8],
    rsne: &NegotiatedRsne,
    msg1: &eapol::KeyFrame,
    snonce: &[u8],
) -> Result<eapol::KeyFrame, failure::Error> {
    let mut key_info = eapol::KeyInformation(0);
    key_info.set_key_descriptor_version(msg1.key_info.key_descriptor_version());
    key_info.set_key_type(msg1.key_info.key_type());
    key_info.set_key_mic(true);

    let mut key_data = vec![];
    cfg.s_rsne.as_bytes(&mut key_data);

    let mut msg2 = eapol::KeyFrame {
        version: msg1.version,
        packet_type: eapol::PacketType::Key as u8,
        packet_body_len: 0, // Updated afterwards
        descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
        key_info: key_info,
        key_len: 0,
        key_replay_counter: msg1.key_replay_counter,
        key_mic: Bytes::from(vec![0u8; msg1.key_mic.len()]),
        key_rsc: 0,
        key_iv: [0u8; 16],
        key_nonce: eapol::to_array(snonce),
        key_data_len: key_data.len() as u16,
        key_data: Bytes::from(key_data),
    };
    msg2.update_packet_body_len();

    msg2.key_mic = Bytes::from(compute_mic(kck, &rsne.akm, &msg2)?);

    Ok(msg2)
}

// IEEE Std 802.11-2016, 12.7.6.4
fn handle_message_3(
    cfg: &Config,
    kck: &[u8],
    kek: &[u8],
    msg3: FourwayHandshakeFrame,
) -> Result<(eapol::KeyFrame, Gtk), failure::Error> {
    let negotiated_rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;
    let frame = match &msg3.get() {
        KeyFrameState::UnverifiedMic(unverified) => {
            unverified.verify_mic(kck, &negotiated_rsne.akm)?
        }
        KeyFrameState::NoMic(_) => bail!("msg3 of 4-Way Handshake must carry a MIC"),
    };

    let key_data = match &msg3.get_key_data() {
        KeyFrameKeyDataState::Unencrypted(_) => {
            bail!("msg3 of 4-Way Handshake must carry encrypted key data")
        }
        KeyFrameKeyDataState::Encrypted(encrypted) => {
            encrypted.decrypt(kek, &negotiated_rsne.akm)?
        }
    };

    let mut gtk: Option<key_data::kde::Gtk> = None;
    let mut rsne: Option<Rsne> = None;
    let mut _second_rsne: Option<Rsne> = None;
    let elements = key_data::extract_elements(&key_data[..])?;
    for ele in elements {
        match (ele, rsne.as_ref()) {
            (key_data::Element::Gtk(_, e), _) => gtk = Some(e),
            (key_data::Element::Rsne(e), None) => rsne = Some(e),
            (key_data::Element::Rsne(e), Some(_)) => _second_rsne = Some(e),
            _ => (),
        }
    }

    // Proceed if key data held a GTK and RSNE and RSNE is the Authenticator's announced one.
    match (gtk, rsne) {
        (Some(gtk), Some(rsne)) => {
            ensure!(&rsne == &cfg.a_rsne, Error::InvalidKeyDataRsne);
            let msg4 = create_message_4(&negotiated_rsne, kck, frame)?;
            let rsc = msg3.frame.key_rsc;
            Ok((msg4, Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), negotiated_rsne.group_data, rsc)?))
        }
        _ => bail!(Error::InvalidKeyDataContent),
    }
}

// IEEE Std 802.11-2016, 12.7.6.5
fn create_message_4(
    rsne: &NegotiatedRsne,
    kck: &[u8],
    msg3: &eapol::KeyFrame,
) -> Result<eapol::KeyFrame, failure::Error> {
    let mut key_info = eapol::KeyInformation(0);
    key_info.set_key_descriptor_version(msg3.key_info.key_descriptor_version());
    key_info.set_key_type(msg3.key_info.key_type());
    key_info.set_key_mic(true);
    key_info.set_secure(true);

    let mut msg4 = eapol::KeyFrame {
        version: msg3.version,
        packet_type: eapol::PacketType::Key as u8,
        packet_body_len: 0, // Updated afterwards
        descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
        key_info: key_info,
        key_len: 0,
        key_replay_counter: msg3.key_replay_counter,
        key_mic: Bytes::from(vec![0u8; msg3.key_mic.len()]),
        key_rsc: 0,
        key_iv: [0u8; 16],
        key_nonce: [0u8; 32],
        key_data_len: 0,
        key_data: Bytes::from(vec![]),
    };
    msg4.update_packet_body_len();

    msg4.key_mic = Bytes::from(compute_mic(kck, &rsne.akm, &msg4)?);

    Ok(msg4)
}

#[derive(Debug, PartialEq)]
pub enum State {
    AwaitingMsg1 { pmk: Vec<u8>, cfg: Config },
    AwaitingMsg3 { pmk: Vec<u8>, ptk: Ptk, anonce: Nonce, cfg: Config },
    KeysInstalled { pmk: Vec<u8>, ptk: Ptk, gtk: Gtk, cfg: Config },
}

pub fn new(cfg: Config, pmk: Vec<u8>) -> State {
    State::AwaitingMsg1 { pmk, cfg }
}

impl State {
    pub fn on_eapol_key_frame(
        self,
        update_sink: &mut UpdateSink,
        frame: FourwayHandshakeFrame,
    ) -> Self {
        match self {
            State::AwaitingMsg1 { pmk, cfg } => {
                // Safe since the frame is only used for deriving the message number.
                match fourway::message_number(frame.get().unsafe_get_raw()) {
                    fourway::MessageNumber::Message1 => {
                        let snonce = match cfg.nonce_rdr.next() {
                            Ok(nonce) => nonce,
                            Err(e) => {
                                error!("error: {}", e);
                                return State::AwaitingMsg1 { pmk, cfg };
                            }
                        };
                        match handle_message_1(&cfg, &pmk[..], &snonce[..], frame) {
                            Err(e) => {
                                error!("error: {}", e);
                                return State::AwaitingMsg1 { pmk, cfg };
                            }
                            Ok((msg2, ptk, anonce)) => {
                                update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg2));
                                State::AwaitingMsg3 { pmk, ptk, cfg, anonce }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!("error: {}", Error::Unexpected4WayHandshakeMessage(unexpected_msg));
                        State::AwaitingMsg1 { pmk, cfg }
                    }
                }
            }
            State::AwaitingMsg3 { pmk, ptk, cfg, .. } => {
                // Safe since the frame is only used for deriving the message number.
                match fourway::message_number(frame.get().unsafe_get_raw()) {
                    // Restart handshake if first message was received.
                    fourway::MessageNumber::Message1 => {
                        State::AwaitingMsg1 { pmk, cfg }.on_eapol_key_frame(update_sink, frame)
                    }
                    // Third message of the handshake can be processed multiple times but PTK and
                    // GTK are only installed once.
                    fourway::MessageNumber::Message3 => {
                        match handle_message_3(&cfg, ptk.kck(), ptk.kek(), frame) {
                            Err(e) => {
                                error!("error: {}", e);
                                State::AwaitingMsg1 { pmk, cfg }
                            }
                            Ok((msg4, gtk)) => {
                                update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg4));
                                update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                                update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
                                State::KeysInstalled { pmk, ptk, gtk, cfg }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!("error: {}", Error::Unexpected4WayHandshakeMessage(unexpected_msg));
                        State::AwaitingMsg1 { pmk, cfg }
                    }
                }
            }
            State::KeysInstalled { ref ptk, gtk: ref expected_gtk, ref cfg, .. } => {
                // Safe since the frame is only used for deriving the message number.
                match fourway::message_number(frame.get().unsafe_get_raw()) {
                    // Allow message 3 replays for robustness but never reinstall PTK or GTK.
                    // Reinstalling keys could create an attack surface for vulnerabilities such as
                    // KRACK.
                    fourway::MessageNumber::Message3 => {
                        match handle_message_3(cfg, ptk.kck(), ptk.kek(), frame) {
                            Err(e) => error!("error: {}", e),
                            // Ensure GTK didn't change. IEEE 802.11-2016 isn't specifying this edge
                            // case and leaves room for interpretation whether or not a replayed
                            // 3rd message can carry a different GTK than originally sent.
                            // Fuchsia decided to require all GTKs to match; if the GTK doesn't
                            // match with the original one Fuchsia drops the received message.
                            Ok((msg4, gtk)) => {
                                if fixed_time_eq(&gtk.gtk[..], &expected_gtk.gtk[..]) {
                                    update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg4));
                                } else {
                                    error!("error: GTK differs in replayed 3rd message");
                                    // TODO(hahnr): Cancel RSNA and deauthenticate from network.
                                    // Client won't be able to recover from this state. For now,
                                    // Authenticator will timeout the client.
                                }
                            }
                        };
                    }
                    unexpected_msg => {
                        error!(
                            "ignoring message {:?}; 4-Way Handshake already completed",
                            unexpected_msg
                        );
                    }
                };

                self
            }
        }
    }

    pub fn anonce(&self) -> Option<&[u8]> {
        match self {
            State::AwaitingMsg1 { .. } => None,
            State::AwaitingMsg3 { anonce, .. } => Some(&anonce[..]),
            State::KeysInstalled { .. } => None,
        }
    }

    pub fn destroy(self) -> fourway::Config {
        match self {
            State::AwaitingMsg1 { cfg, .. } => cfg,
            State::AwaitingMsg3 { cfg, .. } => cfg,
            State::KeysInstalled { cfg, .. } => cfg,
        }
    }
}
