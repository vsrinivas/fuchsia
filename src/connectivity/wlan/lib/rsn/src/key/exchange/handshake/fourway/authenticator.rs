// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_utils::nonce::Nonce;
use crate::key::exchange::handshake::fourway::{self, Config, FourwayHandshakeFrame};
use crate::key::exchange::{compute_mic_from_buf, Key};
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::key_data::kde;
use crate::rsna::{
    derive_key_descriptor_version, Dot11VerifiedKeyFrame, NegotiatedProtection, SecAssocUpdate,
    UnverifiedKeyData, UpdateSink,
};
use crate::Error;
use anyhow::{ensure, format_err};
use log::{error, warn};
use zerocopy::ByteSlice;

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
                let anonce = cfg.nonce_rdr.next();
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

    pub fn on_eapol_key_frame<B: ByteSlice>(
        self,
        update_sink: &mut UpdateSink,
        _krc: u64,
        frame: FourwayHandshakeFrame<B>,
    ) -> Self {
        match self {
            State::Idle { cfg, pmk } => {
                error!("received EAPOL Key frame before initiate 4-Way Handshake");
                State::Idle { cfg, pmk }
            }
            State::AwaitingMsg2 { pmk, cfg, anonce, last_krc } => {
                // Safe since the frame is only used for deriving the message number.
                match frame.message_number() {
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
                                warn!("Unable to process second EAPOL handshake key frame from supplicant: {}", e);
                                State::AwaitingMsg2 { pmk, cfg, anonce, last_krc }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!(
                            "error: {:?}",
                            Error::UnexpectedHandshakeMessage(unexpected_msg.into())
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
) -> Result<(), anyhow::Error> {
    let protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;
    let krc = krc + 1;
    let msg1 = create_message_1(anonce, &protection, krc)?;
    update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg1));
    Ok(())
}

fn process_message_2<B: ByteSlice>(
    update_sink: &mut UpdateSink,
    pmk: &[u8],
    cfg: &Config,
    anonce: &[u8],
    last_krc: u64,
    next_krc: u64,
    frame: FourwayHandshakeFrame<B>,
) -> Result<(Ptk, Gtk), anyhow::Error> {
    let ptk = handle_message_2(&pmk[..], &cfg, &anonce[..], last_krc, frame)?;

    let gtk =
        cfg.gtk_provider.as_ref().expect("GtkProvider is missing").lock().unwrap().get_gtk()?;
    let protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;
    let msg3 =
        create_message_3(&cfg, ptk.kck(), ptk.kek(), &gtk, &anonce[..], &protection, next_krc)?;

    update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg3));
    Ok((ptk, gtk))
}

fn process_message_4<B: ByteSlice>(
    update_sink: &mut UpdateSink,
    cfg: &Config,
    ptk: &Ptk,
    gtk: &Gtk,
    last_krc: u64,
    frame: FourwayHandshakeFrame<B>,
) -> Result<(), anyhow::Error> {
    handle_message_4(cfg, ptk.kck(), last_krc, frame)?;
    update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
    update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
    Ok(())
}

// IEEE Std 802.11-2016, 12.7.6.2
fn create_message_1<B: ByteSlice>(
    anonce: B,
    protection: &NegotiatedProtection,
    krc: u64,
) -> Result<eapol::KeyFrameBuf, anyhow::Error> {
    let version = derive_key_descriptor_version(eapol::KeyDescriptor::IEEE802DOT11, protection);
    let key_info = eapol::KeyInformation(0)
        .with_key_descriptor_version(version)
        .with_key_type(eapol::KeyType::PAIRWISE)
        .with_key_ack(true);

    let key_len = match protection.pairwise.tk_bits() {
        None => {
            return Err(format_err!(
                "unknown cipher used for pairwise key: {:?}",
                protection.pairwise
            ))
        }
        Some(tk_bits) => tk_bits / 8,
    };
    eapol::KeyFrameTx::new(
        eapol::ProtocolVersion::IEEE802DOT1X2004,
        eapol::KeyFrameFields::new(
            eapol::KeyDescriptor::IEEE802DOT11,
            key_info,
            key_len,
            krc,
            eapol::to_array(&anonce),
            [0u8; 16], // iv
            0,         // rsc
        ),
        vec![],
        protection.mic_size as usize,
    )
    .serialize()
    .finalize_without_mic()
    .map_err(|e| e.into())
}

// IEEE Std 802.11-2016, 12.7.6.3
pub fn handle_message_2<B: ByteSlice>(
    pmk: &[u8],
    cfg: &Config,
    anonce: &[u8],
    krc: u64,
    frame: FourwayHandshakeFrame<B>,
) -> Result<Ptk, anyhow::Error> {
    // Safe: The nonce must be accessed to compute the PTK. The frame will still be fully verified
    // before accessing any of its fields.
    let snonce = &frame.unsafe_get_raw().key_frame_fields.key_nonce[..];
    let protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;

    let ptk = Ptk::new(
        pmk,
        &cfg.a_addr,
        &cfg.s_addr,
        anonce,
        snonce,
        &protection.akm,
        protection.pairwise.clone(),
    )?;

    // PTK was computed, verify the frame's MIC.
    let frame = match frame.get() {
        Dot11VerifiedKeyFrame::WithUnverifiedMic(unverified_mic) => {
            match unverified_mic.verify_mic(ptk.kck(), &protection)? {
                UnverifiedKeyData::Encrypted(_) => {
                    return Err(format_err!("msg2 of 4-Way Handshake must not be encrypted"))
                }
                UnverifiedKeyData::NotEncrypted(frame) => frame,
            }
        }
        Dot11VerifiedKeyFrame::WithoutMic(_) => {
            return Err(format_err!("msg2 of 4-Way Handshake must carry a MIC"))
        }
    };
    ensure!(
        frame.key_frame_fields.key_replay_counter.to_native() == krc,
        "error, expected Supplicant response to message {:?} but was {:?} in msg #2",
        krc,
        frame.key_frame_fields.key_replay_counter.to_native()
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
    protection: &NegotiatedProtection,
    krc: u64,
) -> Result<eapol::KeyFrameBuf, anyhow::Error> {
    // Construct key data which contains the Beacon's RSNE and a GTK KDE.
    let mut w = kde::Writer::new(vec![]);
    w.write_protection(&cfg.a_protection)?;
    w.write_gtk(&kde::Gtk::new(gtk.key_id(), kde::GtkInfoTx::BothRxTx, gtk.tk()))?;
    let key_data = w.finalize_for_encryption()?;
    let key_iv = [0u8; 16];
    let encrypted_key_data =
        protection.keywrap_algorithm()?.wrap_key(kek, &key_iv, &key_data[..])?;

    // Construct message.
    let version = derive_key_descriptor_version(eapol::KeyDescriptor::IEEE802DOT11, protection);
    let key_info = eapol::KeyInformation(0)
        .with_key_descriptor_version(version)
        .with_key_type(eapol::KeyType::PAIRWISE)
        .with_key_ack(true)
        .with_key_mic(true)
        .with_install(true)
        .with_secure(true)
        .with_encrypted_key_data(true);

    let key_len = match protection.pairwise.tk_bits() {
        None => {
            return Err(format_err!(
                "unknown cipher used for pairwise key: {:?}",
                protection.pairwise
            ))
        }
        Some(tk_bits) => tk_bits / 8,
    };

    let msg3 = eapol::KeyFrameTx::new(
        eapol::ProtocolVersion::IEEE802DOT1X2004,
        eapol::KeyFrameFields::new(
            eapol::KeyDescriptor::IEEE802DOT11,
            key_info,
            key_len,
            krc,
            eapol::to_array(anonce),
            key_iv,
            0, // rsc
        ),
        encrypted_key_data,
        protection.mic_size as usize,
    )
    .serialize();

    let mic = compute_mic_from_buf(kck, &protection, msg3.unfinalized_buf())
        .map_err(|e| anyhow::Error::from(e))?;
    msg3.finalize_with_mic(&mic[..]).map_err(|e| e.into())
}

// IEEE Std 802.11-2016, 12.7.6.5
pub fn handle_message_4<B: ByteSlice>(
    cfg: &Config,
    kck: &[u8],
    krc: u64,
    frame: FourwayHandshakeFrame<B>,
) -> Result<(), anyhow::Error> {
    let protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;
    let frame = match frame.get() {
        Dot11VerifiedKeyFrame::WithUnverifiedMic(unverified_mic) => {
            match unverified_mic.verify_mic(kck, &protection)? {
                UnverifiedKeyData::Encrypted(_) => {
                    return Err(format_err!("msg4 of 4-Way Handshake must not be encrypted"))
                }
                UnverifiedKeyData::NotEncrypted(frame) => frame,
            }
        }
        Dot11VerifiedKeyFrame::WithoutMic(_) => {
            return Err(format_err!("msg4 of 4-Way Handshake must carry a MIC"))
        }
    };
    ensure!(
        frame.key_frame_fields.key_replay_counter.to_native() == krc,
        "error, expected Supplicant response to message {:?} but was {:?} in msg #4",
        krc,
        frame.key_frame_fields.key_replay_counter.to_native()
    );

    // Note: The message's integrity was already verified by low layers.

    Ok(())
}
