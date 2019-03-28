// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::integrity::{self, integrity_algorithm};
use crate::key::exchange::{
    handshake::group_key::{self, Config, GroupKeyHandshakeFrame},
    Key,
};
use crate::key::gtk::Gtk;
use crate::key_data;
use crate::rsna::{KeyFrameKeyDataState, KeyFrameState, SecAssocUpdate, UpdateSink};
use crate::Error;
use bytes::Bytes;
use eapol;
use failure::{self, bail};

#[derive(Debug, PartialEq)]
pub struct Supplicant {
    pub cfg: group_key::Config,
    pub kck: Bytes,
    pub kek: Bytes,
}

impl Supplicant {
    // IEEE Std 802.11-2016, 12.7.7.2
    pub fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        msg1: GroupKeyHandshakeFrame,
    ) -> Result<(), failure::Error> {
        let frame = match &msg1.get() {
            KeyFrameState::UnverifiedMic(unverified) => {
                let frame = unverified.verify_mic(&self.kck[..], &self.cfg.akm)?;
                frame
            }
            KeyFrameState::NoMic(_) => bail!("msg1 of Group-Key Handshake must carry a MIC"),
        };

        // Extract GTK from data.
        let mut gtk: Option<key_data::kde::Gtk> = None;
        let key_data = match &msg1.get_key_data() {
            KeyFrameKeyDataState::Unencrypted(_) => {
                bail!("msg1 of Group-Key Handshake must carry encrypted key data")
            }
            KeyFrameKeyDataState::Encrypted(encrypted) => {
                encrypted.decrypt(&self.kek[..], &self.cfg.akm)?
            }
        };
        let elements = key_data::extract_elements(&key_data[..])?;
        for ele in elements {
            match ele {
                key_data::Element::Gtk(_, e) => gtk = Some(e),
                _ => {}
            }
        }
        let gtk = match gtk {
            None => bail!("GTK KDE not present in key data of Group-Key Handshakes's 1st message"),
            Some(gtk) => Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), self.cfg.cipher.clone())?,
        };

        // Construct second message of handshake.
        let msg2 = self.create_message_2(frame)?;

        update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg2));
        update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));

        Ok(())
    }

    // IEEE Std 802.11-2016, 12.7.7.3
    fn create_message_2(&self, msg1: &eapol::KeyFrame) -> Result<eapol::KeyFrame, failure::Error> {
        let mut key_info = eapol::KeyInformation(0);
        key_info.set_key_descriptor_version(msg1.key_info.key_descriptor_version());
        key_info.set_key_type(msg1.key_info.key_type());
        key_info.set_key_mic(true);
        key_info.set_secure(true);

        let mut msg2 = eapol::KeyFrame {
            version: msg1.version,
            packet_type: eapol::PacketType::Key as u8,
            packet_body_len: 0, // Updated afterwards
            descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
            key_info: key_info,
            key_len: 0,
            key_replay_counter: msg1.key_replay_counter,
            key_nonce: [0u8; 32],
            key_iv: [0u8; 16],
            key_rsc: 0,
            key_mic: Bytes::from(vec![0u8; msg1.key_mic.len()]),
            key_data_len: 0,
            key_data: Bytes::from(vec![]),
        };
        msg2.update_packet_body_len();

        // Update the frame's MIC.
        let akm = &self.cfg.akm;
        let integrity_alg = integrity_algorithm(&akm).ok_or(Error::UnsupportedAkmSuite)?;
        let mic_len = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)?;
        update_mic(&self.kck[..], mic_len, integrity_alg, &mut msg2)?;

        Ok(msg2)
    }

    pub fn destroy(self) -> Config {
        self.cfg
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
