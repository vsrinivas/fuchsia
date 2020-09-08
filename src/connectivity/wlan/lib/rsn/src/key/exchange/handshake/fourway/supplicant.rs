// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_utils::nonce::Nonce;
use crate::key::exchange::handshake::fourway::{self, Config, FourwayHandshakeFrame};
use crate::key::exchange::{compute_mic_from_buf, Key};
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::key_data;
use crate::key_data::kde;
use crate::rsna::{
    Dot11VerifiedKeyFrame, NegotiatedProtection, ProtectionType, SecAssocUpdate, UnverifiedKeyData,
    UpdateSink,
};
use crate::Error;
use crate::ProtectionInfo;
use anyhow::{ensure, format_err};
use crypto::util::fixed_time_eq;
use eapol;
use eapol::KeyFrameBuf;
use log::error;
use zerocopy::ByteSlice;

// IEEE Std 802.11-2016, 12.7.6.2
fn handle_message_1<B: ByteSlice>(
    cfg: &Config,
    pmk: &[u8],
    snonce: &[u8],
    msg1: FourwayHandshakeFrame<B>,
) -> Result<(KeyFrameBuf, Ptk, Nonce), anyhow::Error> {
    let frame = match msg1.get() {
        // Note: This is only true if PTK re-keying is not supported.
        Dot11VerifiedKeyFrame::WithUnverifiedMic(_) => {
            return Err(format_err!("msg1 of 4-Way Handshake cannot carry a MIC"))
        }
        Dot11VerifiedKeyFrame::WithoutMic(frame) => frame,
    };
    let anonce = frame.key_frame_fields.key_nonce;
    let protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;

    let pairwise = protection.pairwise.clone();
    let ptk =
        Ptk::new(pmk, &cfg.a_addr, &cfg.s_addr, &anonce[..], snonce, &protection.akm, pairwise)?;
    let msg2 = create_message_2(cfg, ptk.kck(), &protection, &frame, &snonce[..])?;

    Ok((msg2, ptk, anonce))
}

// IEEE Std 802.11-2016, 12.7.6.3
fn create_message_2<B: ByteSlice>(
    cfg: &Config,
    kck: &[u8],
    protection: &NegotiatedProtection,
    msg1: &eapol::KeyFrameRx<B>,
    snonce: &[u8],
) -> Result<KeyFrameBuf, anyhow::Error> {
    let key_info = eapol::KeyInformation(0)
        .with_key_descriptor_version(msg1.key_frame_fields.key_info().key_descriptor_version())
        .with_key_type(msg1.key_frame_fields.key_info().key_type())
        .with_key_mic(true);

    let mut w = kde::Writer::new(vec![]);
    w.write_protection(&cfg.s_protection)?;
    let key_data = w.finalize_for_plaintext()?;

    let msg2 = eapol::KeyFrameTx::new(
        msg1.eapol_fields.version,
        eapol::KeyFrameFields::new(
            msg1.key_frame_fields.descriptor_type,
            key_info,
            0,
            msg1.key_frame_fields.key_replay_counter.to_native(),
            eapol::to_array(snonce),
            [0u8; 16], // IV
            0,         // RSC
        ),
        key_data,
        msg1.key_mic.len(),
    )
    .serialize();

    let mic = compute_mic_from_buf(kck, &protection, msg2.unfinalized_buf())
        .map_err(|e| anyhow::Error::from(e))?;
    msg2.finalize_with_mic(&mic[..]).map_err(|e| e.into())
}

// IEEE Std 802.11-2016, 12.7.6.4
// This function will never return an empty GTK unless the protection is WPA1, in which case this is
// not set until a subsequent GroupKey handshake.
fn handle_message_3<B: ByteSlice>(
    cfg: &Config,
    kck: &[u8],
    kek: &[u8],
    msg3: FourwayHandshakeFrame<B>,
) -> Result<(KeyFrameBuf, Option<Gtk>), anyhow::Error> {
    let negotiated_protection = NegotiatedProtection::from_protection(&cfg.s_protection)?;
    let (frame, key_data_elements) = match msg3.get() {
        Dot11VerifiedKeyFrame::WithUnverifiedMic(unverified_mic) => {
            match unverified_mic.verify_mic(kck, &negotiated_protection)? {
                UnverifiedKeyData::Encrypted(encrypted) => {
                    let key_data = encrypted.decrypt(kek, &negotiated_protection)?;
                    (key_data.0, key_data::extract_elements(&key_data.1[..])?)
                }
                UnverifiedKeyData::NotEncrypted(keyframe) => {
                    match negotiated_protection.protection_type {
                        ProtectionType::LegacyWpa1 => {
                            // WFA, WPA1 Spec. 3.1, Chapter 2.2.4
                            // WPA1 does not encrypt the key data field during a 4-way handshake.
                            let elements = key_data::extract_elements(&keyframe.key_data[..])?;
                            (keyframe, elements)
                        }
                        _ => {
                            return Err(format_err!(
                                "msg3 of 4-Way Handshake must carry encrypted key data"
                            ))
                        }
                    }
                }
            }
        }
        Dot11VerifiedKeyFrame::WithoutMic(_) => {
            return Err(format_err!("msg3 of 4-Way Handshake must carry a MIC"))
        }
    };
    let mut gtk: Option<key_data::kde::Gtk> = None;
    let mut protection: Option<ProtectionInfo> = None;
    let mut _second_protection: Option<ProtectionInfo> = None;
    for element in key_data_elements {
        match (element, &protection) {
            (key_data::Element::Gtk(_, e), _) => gtk = Some(e),
            (key_data::Element::Rsne(e), None) => protection = Some(ProtectionInfo::Rsne(e)),
            (key_data::Element::Rsne(e), Some(_)) => {
                _second_protection = Some(ProtectionInfo::Rsne(e))
            }
            (key_data::Element::LegacyWpa1(e), None) => {
                protection = Some(ProtectionInfo::LegacyWpa(e))
            }
            _ => (),
        }
    }

    // Proceed if key data held a protection element matching the Authenticator's announced one.
    let msg4 = match protection {
        Some(protection) => {
            ensure!(&protection == &cfg.a_protection, Error::InvalidKeyDataProtection);
            create_message_4(&negotiated_protection, kck, &frame)?
        }
        None => return Err(format_err!(Error::InvalidKeyDataContent)),
    };
    match gtk {
        Some(gtk) => {
            let rsc = frame.key_frame_fields.key_rsc.to_native();
            Ok((
                msg4,
                Some(Gtk::from_gtk(
                    gtk.gtk,
                    gtk.info.key_id(),
                    negotiated_protection.group_data,
                    rsc,
                )?),
            ))
        }
        // In WPA1 a GTK is not specified until a subsequent GroupKey handshake.
        None if negotiated_protection.protection_type == ProtectionType::LegacyWpa1 => {
            Ok((msg4, None))
        }
        None => return Err(format_err!(Error::InvalidKeyDataContent)),
    }
}

// IEEE Std 802.11-2016, 12.7.6.5
fn create_message_4<B: ByteSlice>(
    protection: &NegotiatedProtection,
    kck: &[u8],
    msg3: &eapol::KeyFrameRx<B>,
) -> Result<KeyFrameBuf, anyhow::Error> {
    // WFA, WPA1 Spec. 3.1, Chapter 2 seems to imply that the secure bit should not be set for WPA1
    // supplicant messages, and in practice this seems to be the case.
    let secure_bit = msg3.key_frame_fields.descriptor_type != eapol::KeyDescriptor::LEGACY_WPA1;
    let key_info = eapol::KeyInformation(0)
        .with_key_descriptor_version(msg3.key_frame_fields.key_info().key_descriptor_version())
        .with_key_type(msg3.key_frame_fields.key_info().key_type())
        .with_key_mic(true)
        .with_secure(secure_bit);

    let msg4 = eapol::KeyFrameTx::new(
        msg3.eapol_fields.version,
        eapol::KeyFrameFields::new(
            msg3.key_frame_fields.descriptor_type,
            key_info,
            0,
            msg3.key_frame_fields.key_replay_counter.to_native(),
            [0u8; 32], // nonce
            [0u8; 16], // iv
            0,         // rsc
        ),
        vec![],
        msg3.key_mic.len(),
    )
    .serialize();

    let mic = compute_mic_from_buf(kck, &protection, msg4.unfinalized_buf())
        .map_err(|e| anyhow::Error::from(e))?;
    msg4.finalize_with_mic(&mic[..]).map_err(|e| e.into())
}

#[derive(Debug, PartialEq)]
pub enum State {
    AwaitingMsg1 { pmk: Vec<u8>, cfg: Config, snonce: Nonce },
    AwaitingMsg3 { pmk: Vec<u8>, ptk: Ptk, snonce: Nonce, anonce: Nonce, cfg: Config },
    KeysInstalled { pmk: Vec<u8>, ptk: Ptk, gtk: Option<Gtk>, cfg: Config },
}

pub fn new(cfg: Config, pmk: Vec<u8>) -> State {
    let snonce = cfg.nonce_rdr.next();
    State::AwaitingMsg1 { pmk, cfg, snonce }
}

impl State {
    pub fn on_eapol_key_frame<B: ByteSlice>(
        self,
        update_sink: &mut UpdateSink,
        frame: FourwayHandshakeFrame<B>,
    ) -> Self {
        match self {
            State::AwaitingMsg1 { pmk, cfg, snonce } => match frame.message_number() {
                fourway::MessageNumber::Message1 => {
                    match handle_message_1(&cfg, &pmk[..], &snonce[..], frame) {
                        Err(e) => {
                            error!("error: {}", e);
                            // Note: No need to generate a new SNonce as the received frame is
                            // dropped.
                            return State::AwaitingMsg1 { pmk, cfg, snonce };
                        }
                        Ok((msg2, ptk, anonce)) => {
                            update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg2));
                            State::AwaitingMsg3 { pmk, ptk, cfg, snonce, anonce }
                        }
                    }
                }
                unexpected_msg => {
                    error!("error: {}", Error::UnexpectedHandshakeMessage(unexpected_msg.into()));
                    // Note: No need to generate a new SNonce as the received frame is dropped.
                    State::AwaitingMsg1 { pmk, cfg, snonce }
                }
            },
            State::AwaitingMsg3 { pmk, ptk, cfg, snonce, anonce: expected_anonce, .. } => {
                match frame.message_number() {
                    // Restart handshake if first message was received.
                    fourway::MessageNumber::Message1 => {
                        // According to our understanding of IEEE 802.11-2016, 12.7.6.2 the
                        // Authenticator and Supplicant should always generate a new nonce when
                        // sending the first or second message of the 4-Way Handshake to its peer.
                        // We encountered some routers in the wild which follow a different
                        // interpretation of this chapter and are not generating a new nonce and
                        // ignoring new nonces sent by its peer. Our security team reviewed this
                        // behavior and decided that it's safe for the Supplicant to re-send its
                        // SNonce and not generate a new one if the following requirements are met:
                        // (1) The Authenticator re-used its ANonce (effectively replaying its
                        //     original first message).
                        // (2) No other message other than the first message of the handshake were
                        //     exchanged and in particular, no key has been installed yet.
                        // (3) The received message carries an increased Key Replay Counter.
                        //
                        // Fuchsia's ESSSA already drops message which violate (3).
                        // (1) and (2) are verified in the Supplicant implementation:
                        // If the third message of the handshake has ever been successfully
                        // established the Supplicant will enter the "KeysInstalled" state which
                        // rejects all messages but replays of the third one. Thus, (2) is met at
                        // all times.
                        // (1) is verified in the following check.
                        let actual_anonce = frame.unsafe_get_raw().key_frame_fields.key_nonce;
                        let snonce = if expected_anonce != actual_anonce {
                            cfg.nonce_rdr.next()
                        } else {
                            snonce
                        };
                        State::AwaitingMsg1 { pmk, cfg, snonce }
                            .on_eapol_key_frame(update_sink, frame)
                    }
                    // Third message of the handshake can be processed multiple times but PTK and
                    // GTK are only installed once.
                    fourway::MessageNumber::Message3 => {
                        match handle_message_3(&cfg, ptk.kck(), ptk.kek(), frame) {
                            Err(e) => {
                                error!("error: {}", e);
                                // Note: No need to generate a new SNonce as the received frame is
                                // dropped.
                                State::AwaitingMsg1 { pmk, cfg, snonce }
                            }
                            Ok((msg4, gtk)) => {
                                update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg4));
                                update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                                if let Some(gtk) = gtk.as_ref() {
                                    update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())))
                                }
                                State::KeysInstalled { pmk, ptk, gtk, cfg }
                            }
                        }
                    }
                    unexpected_msg => {
                        error!(
                            "error: {}",
                            Error::UnexpectedHandshakeMessage(unexpected_msg.into())
                        );
                        // Note: No need to generate a new SNonce as the received frame is dropped.
                        State::AwaitingMsg1 { pmk, cfg, snonce }
                    }
                }
            }
            State::KeysInstalled { ref ptk, gtk: ref expected_gtk, ref cfg, .. } => {
                match frame.message_number() {
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
                            // match with the original one Fuchsia drops the received message. This
                            // includes the case where no GTK has been set.
                            Ok((msg4, gtk)) => {
                                if match (&gtk, expected_gtk) {
                                    (Some(gtk), Some(expected_gtk)) => {
                                        fixed_time_eq(&gtk.gtk[..], &expected_gtk.gtk[..])
                                    }
                                    (None, None) => true,
                                    _ => false,
                                } {
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
