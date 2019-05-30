// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_utils::nonce::Nonce;
use crate::key::exchange::handshake::fourway::{self, Config, FourwayHandshakeFrame};
use crate::key::exchange::{compute_mic, Key};
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::key_data::{self, kde};
use crate::keywrap::keywrap_algorithm;
use crate::rsna::{
    derive_key_descriptor_version, KeyFrameState, NegotiatedRsne, SecAssocUpdate, UpdateSink,
};
use crate::Error;
use bytes::Bytes;
use failure::{self, bail, ensure};
use log::error;

#[derive(Debug, PartialEq)]
pub enum State {
    Idle { pmk: Vec<u8>, cfg: Config },
    AwaitingMsg2 { pmk: Vec<u8>, cfg: Config, anonce: Nonce, last_krc: u64 },
    AwaitingMsg4 { pmk: Vec<u8>, ptk: Ptk, gtk: Gtk, cfg: Config, last_krc: u64 },
    Completed { cfg: Config },
}

pub fn new(cfg: Config, pmk: Vec<u8>) -> State {
    State::Idle { pmk, cfg }
}

impl State {
    pub fn initiate(self, update_sink: &mut UpdateSink, krc: u64) -> Self {
        match self {
            State::Idle { cfg, pmk } => {
                let anonce = match cfg.nonce_rdr.next() {
                    Ok(nonce) => nonce,
                    Err(e) => {
                        error!("error generating anonce: {}", e);
                        return State::Idle { cfg, pmk };
                    }
                };
                match initiate_internal(update_sink, &cfg, krc, &anonce[..]) {
                    Ok(()) => State::AwaitingMsg2 { anonce, cfg, pmk, last_krc: krc + 1 },
                    Err(e) => {
                        error!("error: {}", e);
                        State::Idle { cfg, pmk }
                    }
                }
            }
            other_state => other_state,
        }
    }

    pub fn on_eapol_key_frame(
        self,
        update_sink: &mut UpdateSink,
        _krc: u64,
        frame: FourwayHandshakeFrame,
    ) -> Self {
        match self {
            State::Idle { cfg, pmk } => {
                error!("received EAPOL Key frame before initiate 4-Way Handshake");
                State::Idle { cfg, pmk }
            }
            State::AwaitingMsg2 { pmk, cfg, anonce, last_krc } => {
                // Safe since the frame is only used for deriving the message number.
                match fourway::message_number(frame.get().unsafe_get_raw()) {
                    fourway::MessageNumber::Message2 => {
                        match process_message_2(
                            update_sink,
                            &pmk[..],
                            &cfg,
                            &anonce[..],
                            last_krc,
                            last_krc + 1,
                            frame,
                        ) {
                            Ok((ptk, gtk)) => {
                                State::AwaitingMsg4 { pmk, ptk, gtk, cfg, last_krc: last_krc + 1 }
                            }
                            Err(e) => {
                                error!("error: {}", e);
                                State::AwaitingMsg2 { pmk, cfg, anonce, last_krc }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!(
                            "error: {:?}",
                            Error::Unexpected4WayHandshakeMessage(unexpected_msg)
                        );
                        State::AwaitingMsg2 { pmk, cfg, anonce, last_krc }
                    }
                }
            }
            State::AwaitingMsg4 { pmk, ptk, gtk, cfg, last_krc } => {
                match process_message_4(update_sink, &cfg, &ptk, &gtk, last_krc, frame) {
                    Ok(()) => State::Completed { cfg },
                    Err(e) => {
                        error!("error: {}", e);
                        State::AwaitingMsg4 { pmk, ptk, gtk, cfg, last_krc }
                    }
                }
            }
            other_state => other_state,
        }
    }

    pub fn destroy(self) -> Config {
        match self {
            State::Idle { cfg, .. } => cfg,
            State::AwaitingMsg2 { cfg, .. } => cfg,
            State::AwaitingMsg4 { cfg, .. } => cfg,
            State::Completed { cfg } => cfg,
        }
    }
}

fn initiate_internal(
    update_sink: &mut UpdateSink,
    cfg: &Config,
    krc: u64,
    anonce: &[u8],
) -> Result<(), failure::Error> {
    let rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;
    let krc = krc + 1;
    let msg1 = create_message_1(anonce, &rsne, krc)?;
    update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg1));
    Ok(())
}

fn process_message_2(
    update_sink: &mut UpdateSink,
    pmk: &[u8],
    cfg: &Config,
    anonce: &[u8],
    last_krc: u64,
    next_krc: u64,
    frame: FourwayHandshakeFrame,
) -> Result<(Ptk, Gtk), failure::Error> {
    let ptk = handle_message_2(&pmk[..], &cfg, &anonce[..], last_krc, frame)?;

    let gtk =
        cfg.gtk_provider.as_ref().expect("GtkProvider is missing").lock().unwrap().get_gtk()?;
    let rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;
    let msg3 = create_message_3(&cfg, ptk.kck(), ptk.kek(), &gtk, &anonce[..], &rsne, next_krc)?;

    update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg3));
    Ok((ptk, gtk))
}

fn process_message_4(
    update_sink: &mut UpdateSink,
    cfg: &Config,
    ptk: &Ptk,
    gtk: &Gtk,
    last_krc: u64,
    frame: FourwayHandshakeFrame,
) -> Result<(), failure::Error> {
    handle_message_4(cfg, ptk.kck(), last_krc, frame)?;
    update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
    update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
    Ok(())
}

// IEEE Std 802.11-2016, 12.7.6.2
fn create_message_1(
    anonce: &[u8],
    rsne: &NegotiatedRsne,
    krc: u64,
) -> Result<eapol::KeyFrame, failure::Error> {
    let version = derive_key_descriptor_version(eapol::KeyDescriptor::Ieee802dot11, rsne);
    let mut key_info = eapol::KeyInformation(0);
    key_info.set_key_descriptor_version(version);
    key_info.set_key_type(eapol::KEY_TYPE_PAIRWISE);
    key_info.set_key_ack(true);

    let key_len = match rsne.pairwise.tk_bits() {
        None => bail!("unknown cipher used for pairwise key: {:?}", rsne.pairwise),
        Some(tk_bits) => tk_bits / 8,
    };
    let mut msg1 = eapol::KeyFrame {
        version: eapol::ProtocolVersion::Ieee802dot1x2010 as u8,
        packet_type: eapol::PacketType::Key as u8,
        packet_body_len: 0, // Updated afterwards
        descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
        key_info,
        key_len,
        key_replay_counter: krc,
        key_mic: Bytes::from(vec![0u8; rsne.mic_size as usize]),
        key_rsc: 0,
        key_iv: [0u8; 16],
        key_nonce: eapol::to_array(anonce),
        key_data_len: 0,
        key_data: Bytes::from(vec![]),
    };
    msg1.update_packet_body_len();

    Ok(msg1)
}

// IEEE Std 802.11-2016, 12.7.6.3
pub fn handle_message_2(
    pmk: &[u8],
    cfg: &Config,
    anonce: &[u8],
    krc: u64,
    frame: FourwayHandshakeFrame,
) -> Result<Ptk, failure::Error> {
    // Safe since the frame's nonce must be accessed in order to compute the PTK which allows MIC
    // verification.
    let snonce = &frame.get().unsafe_get_raw().key_nonce[..];
    let rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;

    let ptk = Ptk::new(pmk, &cfg.a_addr, &cfg.s_addr, anonce, snonce, &rsne.akm, rsne.pairwise)?;

    // PTK was computed, verify the frame's MIC.
    let frame = match &frame.get() {
        KeyFrameState::UnverifiedMic(unverified) => unverified.verify_mic(ptk.kck(), &rsne.akm)?,
        KeyFrameState::NoMic(_) => bail!("msg2 of 4-Way Handshake must carry a MIC"),
    };
    ensure!(
        frame.key_replay_counter == krc,
        "error, expected Supplicant response to message {:?} but was {:?} in msg #2",
        krc,
        frame.key_replay_counter
    );

    // TODO(hahnr): Key data must carry RSNE. Verify.

    Ok(ptk)
}

// IEEE Std 802.11-2016, 12.7.6.4
fn create_message_3(
    cfg: &Config,
    kck: &[u8],
    kek: &[u8],
    gtk: &Gtk,
    anonce: &[u8],
    rsne: &NegotiatedRsne,
    krc: u64,
) -> Result<eapol::KeyFrame, failure::Error> {
    // Construct key data which contains the Beacon's RSNE and a GTK KDE.
    // Write RSNE to key data.
    let mut key_data = vec![];
    cfg.a_rsne.as_bytes(&mut key_data);

    // Write GTK KDE to key data.
    let gtk_kde = kde::Gtk::new(gtk.key_id(), kde::GtkInfoTx::BothRxTx, gtk.tk());
    gtk_kde.as_bytes(&mut key_data);

    // Add optional padding and encrypt the key data.
    key_data::add_padding(&mut key_data);
    let encrypted_key_data =
        keywrap_algorithm(&rsne.akm).ok_or(Error::UnsupportedAkmSuite)?.wrap(kek, &key_data[..])?;

    // Construct message.
    let version = derive_key_descriptor_version(eapol::KeyDescriptor::Ieee802dot11, rsne);
    let mut key_info = eapol::KeyInformation(0);
    key_info.set_key_descriptor_version(version);
    key_info.set_key_type(eapol::KEY_TYPE_PAIRWISE);
    key_info.set_key_ack(true);
    key_info.set_key_mic(true);
    key_info.set_install(true);
    key_info.set_secure(true);
    key_info.set_encrypted_key_data(true);

    let key_len = match rsne.pairwise.tk_bits() {
        None => bail!("unknown cipher used for pairwise key: {:?}", rsne.pairwise),
        Some(tk_bits) => tk_bits / 8,
    };
    let mut msg3 = eapol::KeyFrame {
        version: eapol::ProtocolVersion::Ieee802dot1x2010 as u8,
        packet_type: eapol::PacketType::Key as u8,
        packet_body_len: 0, // Updated afterwards
        descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
        key_info,
        key_len,
        key_replay_counter: krc,
        key_mic: Bytes::from(vec![0u8; rsne.mic_size as usize]),
        key_rsc: 0,
        key_iv: [0u8; 16],
        key_nonce: eapol::to_array(anonce),
        key_data_len: encrypted_key_data.len() as u16,
        key_data: Bytes::from(encrypted_key_data),
    };
    msg3.update_packet_body_len();

    // Compute and update the frame's MIC.
    msg3.key_mic = Bytes::from(compute_mic(kck, &rsne.akm, &msg3)?);

    Ok(msg3)
}

// IEEE Std 802.11-2016, 12.7.6.5
pub fn handle_message_4(
    cfg: &Config,
    kck: &[u8],
    krc: u64,
    frame: FourwayHandshakeFrame,
) -> Result<(), failure::Error> {
    let rsne = NegotiatedRsne::from_rsne(&cfg.s_rsne)?;
    let frame = match &frame.get() {
        KeyFrameState::UnverifiedMic(unverified) => unverified.verify_mic(kck, &rsne.akm)?,
        KeyFrameState::NoMic(_) => bail!("msg4 of 4-Way Handshake must carry a MIC"),
    };
    ensure!(
        frame.key_replay_counter == krc,
        "error, expected Supplicant response to message {:?} but was {:?} in msg #4",
        krc,
        frame.key_replay_counter
    );

    // Note: The message's integrity was already verified by low layers.

    Ok(())
}
