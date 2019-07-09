// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::compute_mic_from_buf;
use crate::key::exchange::{
    handshake::group_key::{self, Config, GroupKeyHandshakeFrame},
    Key,
};
use crate::key::gtk::Gtk;
use crate::key_data;
use crate::rsna::{Dot11VerifiedKeyFrame, SecAssocUpdate, UnverifiedKeyData, UpdateSink};
use crate::Error;
use bytes::Bytes;
use eapol;
use eapol::KeyFrameBuf;
use failure::{self, bail};
use zerocopy::ByteSlice;

/// Implementation of 802.11's Group Key Handshake for the Supplicant role.
#[derive(Debug, PartialEq)]
pub struct Supplicant {
    pub cfg: group_key::Config,
    pub kck: Bytes,
    pub kek: Bytes,
}

impl Supplicant {
    // IEEE Std 802.11-2016, 12.7.7.2
    pub fn on_eapol_key_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        msg1: GroupKeyHandshakeFrame<B>,
    ) -> Result<(), failure::Error> {
        let (frame, key_data) = match msg1.get() {
            Dot11VerifiedKeyFrame::WithUnverifiedMic(unverified_mic) => {
                match unverified_mic.verify_mic(&self.kck[..], &self.cfg.akm)? {
                    UnverifiedKeyData::Encrypted(encrypted) => {
                        encrypted.decrypt(&self.kek[..], &self.cfg.akm)?
                    }
                    UnverifiedKeyData::NotEncrypted(_) => {
                        bail!("msg1 of Group-Key Handshake must carry encrypted key data")
                    }
                }
            }
            Dot11VerifiedKeyFrame::WithoutMic(_) => {
                bail!("msg1 of Group-Key Handshake must carry a MIC")
            }
        };

        // Extract GTK from data.
        let mut gtk: Option<key_data::kde::Gtk> = None;
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
                let rsc = frame.key_frame_fields.key_rsc.to_native();
                Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), self.cfg.cipher.clone(), rsc)?
            }
        };

        // Construct second message of handshake.
        let msg2 = self.create_message_2(&frame)?;
        update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg2));
        update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));

        Ok(())
    }

    // IEEE Std 802.11-2016, 12.7.7.3
    fn create_message_2<B: ByteSlice>(
        &self,
        msg1: &eapol::KeyFrameRx<B>,
    ) -> Result<KeyFrameBuf, failure::Error> {
        let key_info = eapol::KeyInformation(0)
            .with_key_descriptor_version(msg1.key_frame_fields.key_info().key_descriptor_version())
            .with_key_type(msg1.key_frame_fields.key_info().key_type())
            .with_key_mic(true)
            .with_secure(true);

        let msg2 = eapol::KeyFrameTx::new(
            msg1.eapol_fields.version,
            eapol::KeyFrameFields::new(
                eapol::KeyDescriptor::IEEE802DOT11,
                key_info,
                0,
                msg1.key_frame_fields.key_replay_counter.to_native(),
                [0u8; 32], // nonce
                [0u8; 16], // IV
                0,         // RSC
            ),
            vec![],
            self.cfg.akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)? as usize,
        )
        .serialize();
        let mic = compute_mic_from_buf(&self.kck[..], &self.cfg.akm, msg2.unfinalized_buf())
            .map_err(|e| failure::Error::from(e))?;
        msg2.finalize_with_mic(&mic[..]).map_err(|e| e.into())
    }

    pub fn destroy(self) -> Config {
        self.cfg
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::key::exchange::handshake::group_key::GroupKey;
    use crate::key_data::kde;
    use crate::rsna::{test_util, Dot11VerifiedKeyFrame, NegotiatedProtection, Role};
    use wlan_common::big_endian::BigEndianU64;
    use wlan_common::ie::rsn::akm::{Akm, PSK};
    use wlan_common::ie::rsn::cipher::{Cipher, CCMP_128};

    const KCK: [u8; 16] = [1; 16];
    const KEK: [u8; 16] = [2; 16];
    const GTK: [u8; 16] = [3; 16];
    const GTK_RSC: u64 = 81234;
    const GTK_KEY_ID: u8 = 2;

    fn make_verified<B: ByteSlice>(
        key_frame: eapol::KeyFrameRx<B>,
        role: Role,
    ) -> Result<Dot11VerifiedKeyFrame<B>, failure::Error> {
        let protection =
            NegotiatedProtection::from_rsne(&test_util::get_s_rsne()).expect("error getting RNSE");
        Dot11VerifiedKeyFrame::from_frame(key_frame, &role, &protection, 0)
    }

    fn fake_msg1() -> eapol::KeyFrameBuf {
        let mut w = kde::Writer::new(vec![]);
        w.write_gtk(&kde::Gtk::new(GTK_KEY_ID, kde::GtkInfoTx::BothRxTx, &GTK[..]))
            .expect("error writing GTK KDE");
        let key_data = w.finalize_for_encryption().expect("error finalizing key data");
        let encrypted_key_data = test_util::encrypt_key_data(&KEK[..], &key_data[..]);

        let mut key_frame_fields: eapol::KeyFrameFields = Default::default();
        key_frame_fields.set_key_info(eapol::KeyInformation(0b01001110000010));
        key_frame_fields.key_rsc = BigEndianU64::from_native(GTK_RSC);
        key_frame_fields.descriptor_type = eapol::KeyDescriptor::IEEE802DOT11;
        let key_frame = eapol::KeyFrameTx::new(
            eapol::ProtocolVersion::IEEE802DOT1X2004,
            key_frame_fields,
            encrypted_key_data,
            16,
        )
        .serialize();
        let mic = compute_mic_from_buf(&KCK[..], &Akm::new_dot11(PSK), key_frame.unfinalized_buf())
            .expect("error updating MIC");
        key_frame.finalize_with_mic(&mic[..]).expect("error finalizing keyframe")
    }

    fn fake_gtk() -> Gtk {
        Gtk::from_gtk(GTK.to_vec(), GTK_KEY_ID, Cipher::new_dot11(CCMP_128), GTK_RSC)
            .expect("error creating expected GTK")
    }

    #[test]
    fn full_supplicant_test() {
        let psk = Akm::new_dot11(PSK);
        let ccmp = Cipher::new_dot11(CCMP_128);
        let mut handshake = GroupKey::new(
            Config { role: Role::Supplicant, akm: psk.clone(), cipher: ccmp.clone() },
            &KCK[..],
            &KEK[..],
        )
        .expect("error creating Group Key Handshake");

        // Let Supplicant handle key frame.
        let mut update_sink = UpdateSink::default();
        let msg1 = fake_msg1();
        let keyframe = msg1.keyframe();
        let msg1_verified =
            make_verified(keyframe, Role::Supplicant).expect("error verifying group frame");
        handshake
            .on_eapol_key_frame(&mut update_sink, 0, msg1_verified)
            .expect("error processing msg1 of Group Key Handshake");

        // Verify correct GTK was derived.
        let actual_gtk = test_util::expect_reported_gtk(&update_sink);
        let expected_gtk = fake_gtk();
        assert_eq!(actual_gtk, expected_gtk);
    }
}
