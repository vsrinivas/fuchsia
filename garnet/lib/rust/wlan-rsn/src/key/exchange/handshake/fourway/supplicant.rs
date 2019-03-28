// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_utils::nonce::Nonce;
use crate::integrity::{self, integrity_algorithm};
use crate::key::exchange::handshake::fourway::{self, Config, FourwayHandshakeFrame};
use crate::key::exchange::Key;
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::key_data;
use crate::rsna::{
    KeyFrameKeyDataState, KeyFrameState, NegotiatedRsne, SecAssocUpdate, UpdateSink,
};
use crate::Error;
use bytes::Bytes;
use eapol;
use failure::{self, bail, ensure};
use log::{error, info};
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

    let integrity_alg = integrity_algorithm(&rsne.akm).ok_or(Error::UnsupportedAkmSuite)?;
    update_mic(kck, rsne.mic_size, integrity_alg, &mut msg2)?;

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
            Ok((msg4, Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), negotiated_rsne.group_data)?))
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

    let integrity_alg = integrity_algorithm(&rsne.akm).ok_or(Error::UnsupportedAkmSuite)?;
    update_mic(kck, rsne.mic_size, integrity_alg, &mut msg4)?;

    Ok(msg4)
}

#[derive(Debug, PartialEq)]
pub enum State {
    AwaitingMsg1 { pmk: Vec<u8>, cfg: Config },
    AwaitingMsg3 { pmk: Vec<u8>, ptk: Ptk, anonce: Nonce, cfg: Config },
    Completed { pmk: Vec<u8>, cfg: Config },
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
                                update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
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
                    // Third message of the handshake is only processed once to prevent replay
                    // attacks.
                    fourway::MessageNumber::Message3 => {
                        match handle_message_3(&cfg, ptk.kck(), ptk.kek(), frame) {
                            Err(e) => {
                                error!("error: {}", e);
                                State::AwaitingMsg1 { pmk, cfg }
                            }
                            Ok((msg4, gtk)) => {
                                update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg4));
                                update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));
                                State::Completed { pmk, cfg }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!("error: {}", Error::Unexpected4WayHandshakeMessage(unexpected_msg));
                        State::AwaitingMsg1 { pmk, cfg }
                    }
                }
            }
            State::Completed { pmk, cfg } => {
                // Safe since the frame is only used for deriving the message number.
                match fourway::message_number(frame.get().unsafe_get_raw()) {
                    // Restart handshake if first message was received to support re-keying.
                    fourway::MessageNumber::Message1 => {
                        info!("restarting already completed 4-Way Handshake");
                        State::AwaitingMsg1 { pmk, cfg }.on_eapol_key_frame(update_sink, frame)
                    }
                    unexpected_msg => {
                        error!(
                            "ignoring message {:?}; 4-Way Handshake already completed",
                            unexpected_msg
                        );
                        State::Completed { pmk, cfg }
                    }
                }
            }
        }
    }

    pub fn anonce(&self) -> Option<&[u8]> {
        match self {
            State::AwaitingMsg1 { .. } => None,
            State::AwaitingMsg3 { anonce, .. } => Some(&anonce[..]),
            State::Completed { .. } => None,
        }
    }

    pub fn destroy(self) -> fourway::Config {
        match self {
            State::AwaitingMsg1 { cfg, .. } => cfg,
            State::AwaitingMsg3 { cfg, .. } => cfg,
            State::Completed { cfg, .. } => cfg,
        }
    }
}

fn update_mic(
    kck: &[u8],
    mic_len: u16,
    alg: Box<integrity::Algorithm>,
    frame: &mut eapol::KeyFrame,
) -> Result<(), failure::Error> {
    let mut buf = Vec::with_capacity(frame.len());
    frame.as_bytes(true, &mut buf);
    let written = buf.len();
    buf.truncate(written);
    let mic = alg.compute(kck, &buf[..])?;
    frame.key_mic = Bytes::from(&mic[..mic_len as usize]);
    Ok(())
}
