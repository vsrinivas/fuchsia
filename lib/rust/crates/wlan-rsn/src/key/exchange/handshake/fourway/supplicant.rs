// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use crypto_utils::nonce::NonceReader;
use eapol;
use failure;
use key::exchange::Key;
use key::exchange::handshake::fourway;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::{SecAssocResult, SecAssocUpdate};
use std::rc::Rc;

#[derive(Default)]
struct PtkInitState {}
struct GtkInitState {}

impl PtkInitState {
    fn on_message_1(
        &self, shared: &mut SharedState, msg1: &eapol::KeyFrame, _plain_data: &[u8]
    ) -> Result<(eapol::KeyFrame, Ptk), failure::Error> {
        let anonce = &msg1.key_nonce;
        let snonce = shared.nonce_rdr.next();
        let rsne = shared.cfg.negotiated_rsne();
        let akm = &rsne.akm_suites[0];
        let cipher = &rsne.pairwise_cipher_suites[0];

        let ptk = Ptk::new(
            &shared.pmk[..],
            &shared.cfg.peer_addr,
            &shared.cfg.sta_addr,
            &anonce[..],
            &snonce[..],
            akm,
            cipher,
        )?;

        let msg2 = self.create_message_2(msg1, &snonce[..])?;

        shared.anonce.copy_from_slice(&anonce[..]);
        shared.kek = ptk.kek().to_vec();
        shared.kck = ptk.kck().to_vec();

        Ok((msg2, ptk))
    }

    fn create_message_2(
        &self, _msg1: &eapol::KeyFrame, _snonce: &[u8]
    ) -> Result<eapol::KeyFrame, failure::Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }
}

impl GtkInitState {
    fn on_message_3(
        &self, _shared: &mut SharedState, _msg3: &eapol::KeyFrame, _plain_data: &[u8]
    ) -> Result<(eapol::KeyFrame, Gtk), failure::Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }

    fn create_message_4(&self, _msg3: &eapol::KeyFrame) -> Result<eapol::KeyFrame, failure::Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }
}

enum State {
    PtkInit(PtkInitState),
    GtkInit(GtkInitState),
    Completed,
}

impl State {
    pub fn on_eapol_key_frame(
        &mut self, shared: &mut SharedState, frame: &eapol::KeyFrame, plain_data: &[u8]
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
        &mut self, shared: &mut SharedState, msg1: &eapol::KeyFrame, plain_data: &[u8]
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
        &mut self, shared: &mut SharedState, msg3: &eapol::KeyFrame, plain_data: &[u8]
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
        let nonce_rdr = NonceReader::new(cfg.sta_addr)?;
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
        &mut self, frame: &eapol::KeyFrame, plain_data: &[u8]
    ) -> SecAssocResult {
        self.state
            .on_eapol_key_frame(&mut self.shared, frame, plain_data)
    }
}
