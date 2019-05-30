// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::{
    compute_mic,
    handshake::group_key::{self, Config, GroupKeyHandshakeFrame},
    Key,
};
use crate::key::gtk::Gtk;
use crate::key_data;
use crate::rsna::{KeyFrameKeyDataState, KeyFrameState, SecAssocUpdate, UpdateSink};
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
            Some(gtk) => {
                let rsc = msg1.frame.key_rsc;
                Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), self.cfg.cipher.clone(), rsc)?
            }
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
        msg2.key_mic = Bytes::from(compute_mic(&self.kck[..], &self.cfg.akm, &msg2)?);

        Ok(msg2)
    }

    pub fn destroy(self) -> Config {
        self.cfg
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::key::exchange::handshake::group_key::GroupKey;
    use crate::key_data::{self, kde};
    use crate::rsna::{test_util, NegotiatedRsne, Role, VerifiedKeyFrame};
    use wlan_common::ie::rsn::akm::{Akm, PSK};
    use wlan_common::ie::rsn::cipher::{Cipher, CCMP_128};

    fn make_verified(
        key_frame: &eapol::KeyFrame,
        role: Role,
    ) -> Result<VerifiedKeyFrame, failure::Error> {
        let rsne = NegotiatedRsne::from_rsne(&test_util::get_s_rsne()).expect("error getting RNSE");
        VerifiedKeyFrame::from_key_frame(&key_frame, &role, &rsne, 0)
    }

    fn fake_msg1() -> eapol::KeyFrame {
        eapol::KeyFrame {
            version: eapol::ProtocolVersion::Ieee802dot1x2004 as u8,
            packet_type: eapol::PacketType::Key as u8,
            descriptor_type: eapol::KeyDescriptor::Ieee802dot11 as u8,
            key_info: eapol::KeyInformation(0b01001110000010),
            key_mic: Bytes::from(vec![0; 16]),
            ..Default::default()
        }
    }

    #[test]
    fn full_supplicant_test() {
        const KCK: [u8; 16] = [1; 16];
        const KEK: [u8; 16] = [2; 16];
        const GTK: [u8; 16] = [3; 16];
        const GTK_RSC: u64 = 81234;
        const GTK_KEY_ID: u8 = 2;

        let psk = Akm::new_dot11(PSK);
        let ccmp = Cipher::new_dot11(CCMP_128);
        let mut handshake = GroupKey::new(
            Config { role: Role::Supplicant, akm: psk.clone(), cipher: ccmp.clone() },
            &KCK[..],
            &KEK[..],
        )
        .expect("error creating Group Key Handshake");

        // Write GTK KDE
        let mut buf = Vec::with_capacity(256);
        let gtk_kde = kde::Gtk::new(GTK_KEY_ID, kde::GtkInfoTx::BothRxTx, &GTK[..]);
        if let key_data::Element::Gtk(hdr, gtk) = gtk_kde {
            hdr.as_bytes(&mut buf);
            gtk.as_bytes(&mut buf);
        }
        key_data::add_padding(&mut buf);
        let encrypted_key_data = test_util::encrypt_key_data(&KEK[..], &buf[..]);

        // Construct key frame.
        let mut key_frame = eapol::KeyFrame {
            key_rsc: GTK_RSC,
            key_data_len: encrypted_key_data.len() as u16,
            key_data: Bytes::from(encrypted_key_data),
            ..fake_msg1()
        };
        key_frame.update_packet_body_len();
        let mic = compute_mic(&KCK[..], &psk, &mut key_frame).expect("error updating MIC");
        key_frame.key_mic = Bytes::from(mic);
        let msg1 =
            make_verified(&key_frame, Role::Supplicant).expect("error verifying group frame");

        // Let Supplicant handle key frame.
        let mut update_sink = UpdateSink::default();
        handshake
            .on_eapol_key_frame(&mut update_sink, 0, msg1)
            .expect("error processing msg1 of Group Key Handshake");

        // Verify correct GTK was derived.
        let actual_gtk = test_util::extract_reported_gtk(&update_sink);
        let expected_gtk = Gtk::from_gtk(GTK.to_vec(), GTK_KEY_ID, ccmp, GTK_RSC)
            .expect("error creating expected GTK");
        assert_eq!(actual_gtk, expected_gtk);
    }
}
