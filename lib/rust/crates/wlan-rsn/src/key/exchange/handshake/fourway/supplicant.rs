// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use akm::Akm;
use bytes::Bytes;
use bytes::BytesMut;
use crypto_utils::nonce::NonceReader;
use eapol;
use failure;
use integrity;
use key::exchange::Key;
use key::exchange::handshake::fourway;
use key::gtk::Gtk;
use key::ptk::Ptk;
use key_data;
use rsna::{SecAssocResult, SecAssocUpdate};
use rsne::Rsne;
use std::rc::Rc;

#[derive(Default)]
struct PtkInitState {}
struct GtkInitState {}

impl PtkInitState {
    // IEEE Std 802.1X-2010, 12.7.6.2
    fn on_message_1(
        &self,
        shared: &mut SharedState,
        msg1: &eapol::KeyFrame,
        _plain_data: &[u8],
    ) -> Result<(eapol::KeyFrame, Ptk), failure::Error> {
        let anonce = &msg1.key_nonce;
        let snonce = shared.nonce_rdr.next();
        let rsne = &shared.cfg.s_rsne;
        let akm = &rsne.akm_suites[0];
        let cipher = &rsne.pairwise_cipher_suites[0];

        let ptk = Ptk::new(
            &shared.pmk[..],
            &shared.cfg.a_addr,
            &shared.cfg.s_addr,
            &anonce[..],
            &snonce[..],
            akm,
            cipher,
        )?;
        shared.anonce.copy_from_slice(&anonce[..]);
        shared.kek = ptk.kek().to_vec();
        shared.kck = ptk.kck().to_vec();

        let msg2 = self.create_message_2(shared, msg1, &snonce[..])?;

        Ok((msg2, ptk))
    }

    // IEEE Std 802.1X-2010, 12.7.6.3
    fn create_message_2(
        &self,
        shared: &SharedState,
        msg1: &eapol::KeyFrame,
        snonce: &[u8],
    ) -> Result<eapol::KeyFrame, failure::Error> {
        let mut key_info = eapol::KeyInformation(0);
        key_info.set_key_descriptor_version(msg1.key_info.key_descriptor_version());
        key_info.set_key_type(msg1.key_info.key_type());
        key_info.set_key_mic(true);

        let mut key_data = BytesMut::with_capacity(shared.cfg.a_rsne.len());
        shared.cfg.a_rsne.as_bytes(&mut key_data)?;

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
            key_data: key_data.freeze(),
        };
        msg2.update_packet_body_len();

        // Verified before that Supplicant's RSNE holds one AKM Suite.
        let akm = &shared.cfg.s_rsne.akm_suites[0];
        let integrity_alg = akm.integrity_algorithm().ok_or(Error::UnsupportedAkmSuite)?;
        let mic_len = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        update_mic(&shared.kck[..], mic_len, integrity_alg, &mut msg2)?;

        Ok(msg2)
    }
}

impl GtkInitState {
    // IEEE Std 802.1X-2010, 12.7.6.4
    fn on_message_3(
        &self,
        shared: &mut SharedState,
        msg3: &eapol::KeyFrame,
        plain_data: &[u8],
    ) -> Result<(eapol::KeyFrame, Gtk), failure::Error> {
        shared.key_replay_counter = msg3.key_replay_counter;

        let mut gtk: Option<key_data::kde::Gtk> = None;
        let mut rsne: Option<Rsne> = None;
        let mut _second_rsne: Option<Rsne> = None;
        let elements = key_data::extract_elements(plain_data)?;
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
                if &rsne == &shared.cfg.a_rsne {
                    let msg4 = self.create_message_4(shared, msg3)?;
                    Ok((msg4, Gtk::from_gtk(gtk.gtk)))
                } else {
                    Err(Error::InvalidKeyDataRsne.into())
                }
            }
            _ => Err(Error::InvalidKeyDataContent.into()),
        }
    }

    // IEEE Std 802.1X-2010, 12.7.6.5
    fn create_message_4(
        &self,
        shared: &SharedState,
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

        // Verified before that Supplicant's RSNE holds one AKM Suite.
        let akm = &shared.cfg.s_rsne.akm_suites[0];
        let integrity_alg = akm.integrity_algorithm().ok_or(Error::UnsupportedAkmSuite)?;
        let mic_len = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        update_mic(&shared.kck[..], mic_len, integrity_alg, &mut msg4)?;

        Ok(msg4)
    }
}

enum State {
    PtkInit(PtkInitState),
    GtkInit(GtkInitState),
    Completed,
}

impl State {
    pub fn on_eapol_key_frame(
        &mut self,
        shared: &mut SharedState,
        frame: &eapol::KeyFrame,
        plain_data: &[u8],
    ) -> SecAssocResult {
        match fourway::message_number(frame) {
            // Only process first and third message of the Handshake.
            fourway::MessageNumber::Message1 => self.on_message_1(shared, frame, plain_data),
            fourway::MessageNumber::Message3 => self.on_message_3(shared, frame, plain_data),
            // Drop any other message with an error.
            unexpected_msg => Err(Error::Unexpected4WayHandshakeMessage(unexpected_msg).into()),
        }
    }

    fn on_message_1(
        &mut self,
        shared: &mut SharedState,
        msg1: &eapol::KeyFrame,
        plain_data: &[u8],
    ) -> SecAssocResult {
        // Always reset Handshake when first message was received.
        match self {
            // If the Handshake already completed, simply drop the message.
            State::Completed => return Ok(vec![]),
            // If the Handshake is already advanced further, restart the entire Handshake.
            State::GtkInit(_) => *self = State::PtkInit(PtkInitState {}),
            // Else, if the message was expected, proceed.
            State::PtkInit(_) => (),
        };

        // Only the PTK-Init state processes the first message.
        match self {
            State::PtkInit(ptk_init) => {
                let (msg2, ptk) = ptk_init.on_message_1(shared, msg1, plain_data)?;
                // If the first message was processed successfully the PTK is known and the GTK
                // can be exchanged with the Authenticator. Move the state machine forward.
                *self = State::GtkInit(GtkInitState {});
                Ok(vec![
                    SecAssocUpdate::TxEapolKeyFrame(msg2),
                    SecAssocUpdate::Key(Key::Ptk(ptk)),
                ])
            }
            // This should never happen.
            _ => panic!("tried to process first message of 4-Way Handshake in illegal state"),
        }
    }

    fn on_message_3(
        &mut self,
        shared: &mut SharedState,
        msg3: &eapol::KeyFrame,
        plain_data: &[u8],
    ) -> SecAssocResult {
        // Third message of Handshake is only processed once to prevent replay attacks such as
        // KRACK. A replayed third message will be dropped and has no effect on the Supplicant.
        // TODO(hahnr): Decide whether this client side fix should be kept, which will reduce
        // reliability, or if instead the Supplicant should trust the Authenticator to be patched.
        match self {
            State::GtkInit(gtk_init) => {
                let (msg4, gtk) = gtk_init.on_message_3(shared, msg3, plain_data)?;
                // The third message was successfully processed and the handshake completed.
                *self = State::Completed;
                Ok(vec![
                    SecAssocUpdate::TxEapolKeyFrame(msg4),
                    SecAssocUpdate::Key(Key::Gtk(gtk)),
                ])
            }
            // At this point keys are either already installed and this message is a replay of a
            // previous one, or, the message was received before the first message. In any case,
            // drop the frame.
            _ => Ok(vec![]),
        }
    }
}

struct SharedState {
    key_replay_counter: u64,
    anonce: [u8; 32],
    pmk: Vec<u8>,
    kek: Vec<u8>,
    kck: Vec<u8>,
    cfg: Rc<fourway::Config>,
    nonce_rdr: NonceReader,
}

pub struct Supplicant {
    shared: SharedState,
    state: State,
}

impl Supplicant {
    pub fn new(cfg: Rc<fourway::Config>, pmk: Vec<u8>) -> Result<Self, failure::Error> {
        let nonce_rdr = NonceReader::new(cfg.s_addr)?;
        Ok(Supplicant {
            state: State::PtkInit(PtkInitState {}),
            shared: SharedState {
                key_replay_counter: 0,
                anonce: [0u8; 32],
                pmk: pmk,
                kek: vec![],
                kck: vec![],
                cfg,
                nonce_rdr,
            },
        })
    }

    pub fn anonce(&self) -> &[u8] {
        &self.shared.anonce[..]
    }

    pub fn key_replay_counter(&self) -> u64 {
        self.shared.key_replay_counter
    }

    pub fn on_eapol_key_frame(
        &mut self,
        frame: &eapol::KeyFrame,
        plain_data: &[u8],
    ) -> SecAssocResult {
        self.state
            .on_eapol_key_frame(&mut self.shared, frame, plain_data)
    }
}

fn update_mic(
    kck: &[u8],
    mic_len: u16,
    alg: Box<integrity::Algorithm>,
    frame: &mut eapol::KeyFrame,
) -> Result<(), failure::Error> {
    let mut buf = BytesMut::with_capacity(frame.len());
    frame.as_bytes(true, &mut buf)?;
    let written = buf.len();
    buf.truncate(written);
    let mic = alg.compute(kck, &buf[..])?;
    frame.key_mic = Bytes::from(&mic[..mic_len as usize]);
    Ok(())
}
