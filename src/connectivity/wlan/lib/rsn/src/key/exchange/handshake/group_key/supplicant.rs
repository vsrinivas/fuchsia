// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::compute_mic_from_buf;
use crate::key::exchange::{
    handshake::group_key::{self, Config, GroupKeyHandshakeFrame},
    Key,
};
use crate::key::{gtk::Gtk, igtk::Igtk};
use crate::key_data;
use crate::key_data::kde::GtkInfoTx;
use crate::rsna::{
    Dot11VerifiedKeyFrame, IgtkSupport, ProtectionType, SecAssocUpdate, UnverifiedKeyData,
    UpdateSink,
};
use crate::{format_rsn_err, Error};
use bytes::Bytes;
use eapol;
use eapol::KeyFrameBuf;
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
    ) -> Result<(), Error> {
        let (frame, key_data) = match msg1.get() {
            Dot11VerifiedKeyFrame::WithUnverifiedMic(unverified_mic) => {
                match unverified_mic.verify_mic(&self.kck[..], &self.cfg.protection)? {
                    UnverifiedKeyData::Encrypted(encrypted) => {
                        encrypted.decrypt(&self.kek[..], &self.cfg.protection)?
                    }
                    UnverifiedKeyData::NotEncrypted(keyframe) => {
                        match self.cfg.protection.protection_type {
                            ProtectionType::LegacyWpa1 => {
                                // WFA, WPA1 Spec. 3.1, Chapter 2.2.4
                                // WPA1 GTK is encrypted but does not set the encrypted bit, so we
                                // handle it as a special case.
                                let algorithm = &self.cfg.protection.keywrap_algorithm()?;
                                let key_data = algorithm.unwrap_key(
                                    &self.kek[..],
                                    &keyframe.key_frame_fields.key_iv,
                                    &keyframe.key_data[..],
                                )?;
                                (keyframe, key_data)
                            }
                            _ => {
                                return Err(format_rsn_err!(
                                    "msg1 of Group-Key Handshake must carry encrypted key data"
                                ))
                            }
                        }
                    }
                }
            }
            Dot11VerifiedKeyFrame::WithoutMic(_) => {
                return Err(format_rsn_err!("msg1 of Group-Key Handshake must carry a MIC"))
            }
        };

        // Extract GTK from data.
        let (gtk, igtk) = self.extract_key_data(&frame, &key_data[..])?;

        match (igtk.is_some(), self.cfg.protection.igtk_support()) {
            (true, IgtkSupport::Unsupported) | (false, IgtkSupport::Required) => {
                return Err(Error::InvalidKeyDataContent);
            }
            _ => (),
        }

        // Construct second message of handshake.
        let msg2 = self.create_message_2(&frame)?;
        update_sink.push(SecAssocUpdate::TxEapolKeyFrame(msg2));
        update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));
        if let Some(igtk) = igtk {
            update_sink.push(SecAssocUpdate::Key(Key::Igtk(igtk)));
        }

        Ok(())
    }

    fn extract_key_data<B: ByteSlice>(
        &self,
        frame: &eapol::KeyFrameRx<B>,
        key_data: &[u8],
    ) -> Result<(Gtk, Option<Igtk>), Error> {
        let (gtk, igtk) = match self.cfg.protection.protection_type {
            ProtectionType::LegacyWpa1 => {
                let key_id = frame.key_frame_fields.key_info().legacy_wpa1_key_id() as u8;
                (key_data::kde::Gtk::new(key_id, GtkInfoTx::BothRxTx, key_data), None)
            }
            _ => {
                let mut gtk = None;
                let mut igtk = None;
                for element in key_data::extract_elements(key_data)? {
                    match element {
                        key_data::Element::Gtk(_, e) => gtk = Some(e),
                        key_data::Element::Igtk(_, e) => igtk = Some(e),
                        _ => (),
                    }
                }
                (
                    gtk.ok_or(format_rsn_err!(
                        "GTK KDE not present in key data of Group-Key Handshakes's 1st message"
                    ))?,
                    igtk,
                )
            }
        };

        let rsc = frame.key_frame_fields.key_rsc.to_native();
        Ok((
            Gtk::from_gtk(gtk.gtk, gtk.info.key_id(), self.cfg.protection.group_data.clone(), rsc)?,
            igtk.map(|element| Igtk::from_kde(element, self.cfg.protection.group_mgmt_cipher())),
        ))
    }

    // IEEE Std 802.11-2016, 12.7.7.3
    fn create_message_2<B: ByteSlice>(
        &self,
        msg1: &eapol::KeyFrameRx<B>,
    ) -> Result<KeyFrameBuf, Error> {
        let mut key_info = eapol::KeyInformation(0)
            .with_key_descriptor_version(msg1.key_frame_fields.key_info().key_descriptor_version())
            .with_key_type(msg1.key_frame_fields.key_info().key_type())
            .with_key_mic(true)
            .with_secure(true);

        if msg1.key_frame_fields.descriptor_type == eapol::KeyDescriptor::LEGACY_WPA1 {
            key_info = key_info
                .with_legacy_wpa1_key_id(msg1.key_frame_fields.key_info().legacy_wpa1_key_id());
        }

        let msg2 = eapol::KeyFrameTx::new(
            msg1.eapol_fields.version,
            eapol::KeyFrameFields::new(
                msg1.key_frame_fields.descriptor_type,
                key_info,
                0,
                msg1.key_frame_fields.key_replay_counter.to_native(),
                [0u8; 32], // nonce
                [0u8; 16], // IV
                0,         // RSC
            ),
            vec![],
            self.cfg.protection.akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)? as usize,
        )
        .serialize();
        let mic =
            compute_mic_from_buf(&self.kck[..], &self.cfg.protection, msg2.unfinalized_buf())?;
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
    use wlan_common::ie::rsn::cipher::{Cipher, BIP_CMAC_128, CCMP_128, TKIP};
    use wlan_common::organization::Oui;

    const KCK: [u8; 16] = [1; 16];
    const KEK: [u8; 16] = [2; 16];
    const GTK: [u8; 16] = [3; 16];
    const IGTK: [u8; 16] = [4; 16];
    const WPA1_GTK: [u8; 32] = [3; 32];
    const GTK_RSC: u64 = 81234;
    const GTK_KEY_ID: u8 = 2;
    const IGTK_IPN: [u8; 6] = [0xab; 6];
    const IGTK_KEY_ID: u16 = 4;

    fn make_verified<B: ByteSlice>(
        key_frame: eapol::KeyFrameRx<B>,
        role: Role,
        protection: &NegotiatedProtection,
    ) -> Result<Dot11VerifiedKeyFrame<B>, Error> {
        Dot11VerifiedKeyFrame::from_frame(key_frame, &role, protection, 0)
    }

    enum Msg1Config {
        Wpa2,
        Wpa3,
    }

    fn fake_msg1(protection_type: Msg1Config) -> eapol::KeyFrameBuf {
        let mut w = kde::Writer::new(vec![]);
        w.write_gtk(&kde::Gtk::new(GTK_KEY_ID, kde::GtkInfoTx::BothRxTx, &GTK[..]))
            .expect("error writing GTK KDE");
        if let Msg1Config::Wpa3 = &protection_type {
            w.write_igtk(&kde::Igtk::new(IGTK_KEY_ID, &IGTK_IPN[..], &IGTK[..]))
                .expect("error writing IGTK KDE");
        }
        let key_data = w.finalize_for_encryption().expect("error finalizing key data");
        let encrypted_key_data =
            test_util::encrypt_key_data(&KEK[..], &test_util::get_rsne_protection(), &key_data[..]);

        let mut key_info = eapol::KeyInformation::default();
        key_info.set_key_ack(true);
        key_info.set_key_mic(true);
        key_info.set_secure(true);
        key_info.set_encrypted_key_data(true);
        key_info.set_key_descriptor_version(match &protection_type {
            Msg1Config::Wpa2 => 2,
            Msg1Config::Wpa3 => 0,
        });
        let mut key_frame_fields: eapol::KeyFrameFields = Default::default();
        key_frame_fields.set_key_info(key_info);
        key_frame_fields.key_rsc = BigEndianU64::from_native(GTK_RSC);
        key_frame_fields.descriptor_type = eapol::KeyDescriptor::IEEE802DOT11;
        let key_frame = eapol::KeyFrameTx::new(
            eapol::ProtocolVersion::IEEE802DOT1X2004,
            key_frame_fields,
            encrypted_key_data,
            16,
        )
        .serialize();
        let protection = match &protection_type {
            Msg1Config::Wpa2 => test_util::get_rsne_protection(),
            Msg1Config::Wpa3 => test_util::get_wpa3_protection(),
        };
        let mic = compute_mic_from_buf(&KCK[..], &protection, key_frame.unfinalized_buf())
            .expect("error updating MIC");
        key_frame.finalize_with_mic(&mic[..]).expect("error finalizing keyframe")
    }

    fn fake_gtk() -> Gtk {
        Gtk::from_gtk(GTK.to_vec(), GTK_KEY_ID, Cipher::new_dot11(CCMP_128), GTK_RSC)
            .expect("error creating expected GTK")
    }

    fn fake_igtk() -> Igtk {
        Igtk {
            igtk: IGTK.to_vec(),
            ipn: IGTK_IPN,
            key_id: IGTK_KEY_ID,
            cipher: Cipher::new_dot11(BIP_CMAC_128),
        }
    }

    fn fake_msg1_wpa1_deprecated() -> eapol::KeyFrameBuf {
        let encrypted_key_data =
            test_util::encrypt_key_data(&KEK[..], &test_util::get_wpa1_protection(), &WPA1_GTK[..]);

        let mut key_frame_fields: eapol::KeyFrameFields = Default::default();
        key_frame_fields.set_key_info(
            eapol::KeyInformation::default()
                .with_key_descriptor_version(1)
                .with_legacy_wpa1_key_id(GTK_KEY_ID as u16)
                .with_key_ack(true)
                .with_key_mic(true)
                .with_secure(true),
        );
        key_frame_fields.key_rsc = BigEndianU64::from_native(GTK_RSC);
        key_frame_fields.descriptor_type = eapol::KeyDescriptor::LEGACY_WPA1;
        let key_frame = eapol::KeyFrameTx::new(
            eapol::ProtocolVersion::IEEE802DOT1X2001,
            key_frame_fields,
            encrypted_key_data,
            16,
        )
        .serialize();
        let mic = compute_mic_from_buf(
            &KCK[..],
            &test_util::get_wpa1_protection(),
            key_frame.unfinalized_buf(),
        )
        .expect("error updating MIC");
        key_frame.finalize_with_mic(&mic[..]).expect("error finalizing keyframe")
    }

    fn fake_gtk_wpa1() -> Gtk {
        Gtk::from_gtk(
            WPA1_GTK.to_vec(),
            GTK_KEY_ID,
            Cipher { oui: Oui::MSFT, suite_type: TKIP },
            GTK_RSC,
        )
        .expect("error creating expected GTK")
    }

    #[test]
    fn full_supplicant_test() {
        let protection = test_util::get_rsne_protection();
        let mut handshake = GroupKey::new(
            Config { role: Role::Supplicant, protection: protection.clone() },
            &KCK[..],
            &KEK[..],
        )
        .expect("error creating Group Key Handshake");

        // Let Supplicant handle key frame.
        let mut update_sink = UpdateSink::default();
        let msg1 = fake_msg1(Msg1Config::Wpa2);
        let keyframe = msg1.keyframe();
        let msg1_verified = make_verified(keyframe, Role::Supplicant, &protection)
            .expect("error verifying group frame");
        handshake
            .on_eapol_key_frame(&mut update_sink, 0, msg1_verified)
            .expect("error processing msg1 of Group Key Handshake");

        // Verify correct GTK was derived.
        let actual_gtk = test_util::expect_reported_gtk(&update_sink);
        let expected_gtk = fake_gtk();
        assert_eq!(actual_gtk, expected_gtk);
    }

    #[test]
    fn full_wpa3_supplicant_test() {
        let protection = test_util::get_wpa3_protection();
        let mut handshake = GroupKey::new(
            Config { role: Role::Supplicant, protection: protection.clone() },
            &KCK[..],
            &KEK[..],
        )
        .expect("error creating Group Key Handshake");

        // Let Supplicant handle key frame.
        let mut update_sink = UpdateSink::default();
        let msg1 = fake_msg1(Msg1Config::Wpa3);
        let keyframe = msg1.keyframe();
        let msg1_verified = make_verified(keyframe, Role::Supplicant, &protection)
            .expect("error verifying group frame");
        handshake
            .on_eapol_key_frame(&mut update_sink, 0, msg1_verified)
            .expect("error processing msg1 of Group Key Handshake");

        // Verify correct GTK was derived.
        let actual_gtk = test_util::expect_reported_gtk(&update_sink);
        let expected_gtk = fake_gtk();
        assert_eq!(actual_gtk, expected_gtk);
        let actual_igtk = test_util::expect_reported_igtk(&update_sink);
        let expected_igtk = fake_igtk();
        assert_eq!(actual_igtk, expected_igtk);
    }

    #[test]
    fn full_wpa1_supplicant_test() {
        let protection = test_util::get_wpa1_protection();
        let mut handshake = GroupKey::new(
            Config { role: Role::Supplicant, protection: protection.clone() },
            &KCK[..],
            &KEK[..],
        )
        .expect("error creating Group Key Handshake");

        // Let Supplicant handle key frame.
        let mut update_sink = UpdateSink::default();
        let msg1 = fake_msg1_wpa1_deprecated();
        let keyframe = msg1.keyframe();
        let msg1_verified = make_verified(keyframe, Role::Supplicant, &protection)
            .expect("error verifying group frame");
        handshake
            .on_eapol_key_frame(&mut update_sink, 0, msg1_verified)
            .expect("error processing msg1 of Group Key Handshake");

        // Verify correct GTK was derived.
        let actual_gtk = test_util::expect_reported_gtk(&update_sink);
        let expected_gtk = fake_gtk_wpa1();
        assert_eq!(actual_gtk, expected_gtk);
    }
}
