// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use auth;
use eapol;
use failure;
use key::exchange;
use key::exchange::Key;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::SecAssocUpdate;
use rsna::{Role, SecAssocResult};
use rsne::Rsne;
use std::mem;

struct Pmksa {
    method: auth::Method,
    pmk: Option<Vec<u8>>,
}

enum Ptksa {
    Uninitialized(Option<exchange::Config>),
    Initialized(PtksaCfg),
}

impl Ptksa {
    fn initialize(&mut self, pmk: Vec<u8>) -> Result<(), failure::Error> {
        let cfg_option = match self {
            Ptksa::Uninitialized(cfg) => cfg.take(),
            _ => None,
        };
        match cfg_option {
            Some(cfg) => {
                *self = Ptksa::Initialized(PtksaCfg {
                    method: exchange::Method::from_config(cfg, pmk)?,
                    ptk: None,
                });
            }
            _ => (),
        }
        Ok(())
    }

    pub fn by_mut_ref(&mut self) -> &mut Self {
        self
    }
}

struct PtksaCfg {
    method: exchange::Method,
    ptk: Option<Ptk>,
}

// IEEE Std 802.11-2016, 12.6.1.3.2
pub struct EssSa {
    // Configuration.
    role: Role,

    // Security associations.
    pmksa: Pmksa,
    ptksa: Ptksa,
    // TODO(hahnr): Add GTK and optional IGTK support.
}

impl EssSa {
    pub fn new(
        role: Role,
        auth_cfg: auth::Config,
        ptk_exch_cfg: exchange::Config,
    ) -> Result<EssSa, failure::Error> {
        let auth_method = auth::Method::from_config(auth_cfg)?;

        let mut rsna = EssSa {
            role,
            pmksa: Pmksa {
                method: auth_method,
                pmk: None,
            },
            ptksa: Ptksa::Uninitialized(Some(ptk_exch_cfg)),
        };
        rsna.init_pmksa()?;
        Ok(rsna)
    }

    fn on_key_confirmed(&mut self, key: Key) -> Result<(), failure::Error> {
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.pmk = Some(pmk);
                self.init_ptksa()
            }
            Key::Ptk(ptk) => {
                if let Ptksa::Initialized(ptksa) = self.ptksa.by_mut_ref() {
                    ptksa.ptk = Some(ptk);
                }
                // TODO(hahnr): Received new PTK. Invalidate GTKSA if it was already established.
                Ok(())
            }
            Key::Gtk(_gtk) => {
                // TODO(hahnr): Update GTKSA
                // Once both, PTKSA and GTKSA were established, install keys.
                Ok(())
            }
            _ => Ok(()),
        }
    }

    fn init_pmksa(&mut self) -> Result<(), failure::Error> {
        // PSK allows deriving the PMK without exchanging
        let pmk = match self.pmksa.method.by_ref() {
            auth::Method::Psk(psk) => Some(psk.compute()),
            _ => None,
        };
        if let Some(pmk_data) = pmk {
            self.on_key_confirmed(Key::Pmk(pmk_data))?;
        }

        // TODO(hahnr): Support 802.1X authentication if STA is Authenticator and authentication
        // method is not PSK.

        Ok(())
    }

    fn init_ptksa(&mut self) -> Result<(), failure::Error> {
        match self.pmksa.pmk.as_ref() {
            None => Err(Error::PmksaNotEstablished.into()),
            Some(pmk) => self.ptksa.initialize(pmk.to_vec()),
        }
    }

    pub fn on_eapol_frame(&mut self, frame: &eapol::Frame) -> SecAssocResult {
        // Only processes EAPOL Key frames. Drop all other frames silently.
        let updates = match frame {
            &eapol::Frame::Key(ref key_frame) => self.on_eapol_key_frame(&key_frame),
            _ => Ok(vec![]),
        }?;

        // Track keys to correctly update corresponding security associations.
        for update in &updates {
            if let SecAssocUpdate::Key(key) = update {
                self.on_key_confirmed(key.clone())?;
            }
        }

        Ok(updates)
    }

    fn on_eapol_key_frame(&mut self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.pmk {
            None => self.pmksa.method.on_eapol_key_frame(frame),
            Some(_) => match self.ptksa.by_mut_ref() {
                Ptksa::Uninitialized(_) => Ok(vec![]),
                Ptksa::Initialized(ptksa) => ptksa.method.on_eapol_key_frame(frame),
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use akm::{self, Akm};
    use bytes::Bytes;
    use bytes::BytesMut;
    use cipher::{self, Cipher};
    use crypto_utils::nonce::NonceReader;
    use hex::FromHex;
    use rsna::test_util;
    use suite_selector::OUI;

    // Tests WPA2 with CCMP-128 for its Pairwise and Group Cipher with PSK authentication for Supplicant role.
    #[test]
    fn test_supplicant_wpa2_ccmp128_psk() {
        // Create ESS Security Association
        let mut esssa = test_util::get_esssa();

        // Send first message of 4-Way Handshake to Supplicant
        let anonce = test_util::get_nonce();
        let key_replay_counter = 1u64;
        let msg1 = eapol::Frame::Key(test_util::get_4whs_msg1(&anonce[..], key_replay_counter));
        let updates = esssa
            .on_eapol_frame(&msg1)
            .expect("error processing first 4-Way Handshake method");

        // Verify Supplicant's response.
        // Supplicant should only respond with second message of 4-Way Handshake.
        // It shouldn't yield keys until the PTKSA and GTKSA were established.
        assert_eq!(updates.len(), 1);

        let mut ptk: Option<Ptk> = None;
        match updates
            .get(0)
            .expect("expected at least one response from Supplicant")
        {
            SecAssocUpdate::TxEapolKeyFrame(msg2) => {
                // Verify second message.
                let snonce = msg2.key_nonce;
                let a_rsne = test_util::get_a_rsne();
                let a_rsne_data = test_util::get_rsne_bytes(&a_rsne);

                assert_eq!(msg2.version, 1);
                assert_eq!(msg2.packet_type, 3);
                assert_eq!(msg2.packet_body_len as usize, msg2.len() - 4);
                assert_eq!(msg2.descriptor_type, 2);
                assert_eq!(msg2.key_info.value(), 0x010A);
                assert_eq!(msg2.key_len, 0);
                assert_eq!(msg2.key_replay_counter, key_replay_counter);
                assert!(!test_util::is_zero(&msg2.key_nonce[..]));
                assert!(test_util::is_zero(&msg2.key_iv[..]));
                assert_eq!(msg2.key_rsc, 0);
                assert!(!test_util::is_zero(&msg2.key_mic[..]));
                assert_eq!(msg2.key_mic.len(), test_util::mic_len());
                assert_eq!(msg2.key_data.len(), msg2.key_data_len as usize);
                assert_eq!(msg2.key_data.len(), a_rsne_data.len());
                assert_eq!(&msg2.key_data[..], &a_rsne_data[..]);

                // Verify the message's MIC.
                let derived_ptk = test_util::get_ptk(&anonce[..], &snonce[..]);
                let mic = test_util::compute_mic(derived_ptk.kck(), &msg2);
                assert_eq!(&msg2.key_mic[..], &mic[..]);
                ptk = Some(derived_ptk);
            }
            _ => assert!(
                false,
                "Supplicant sent updates other than Handshake's 2nd message"
            ),
        }

        // Supplicant should be configured with PTK now and wait for the Authenticator to send GTK.
        let ptk = ptk.expect("expected PTK to be derived");

        // Send third message of 4-Way Handshake to Supplicant.
        let key_replay_counter = 2u64;
        let gtk = vec![42u8; 16];
        let key_frame = test_util::get_4whs_msg3(&ptk, &anonce[..], key_replay_counter, &gtk[..]);
        let msg3 = eapol::Frame::Key(key_frame);
        let updates = esssa
            .on_eapol_frame(&msg3)
            .expect("error processing third 4-Way Handshake method");

        // Expecting three updates: PTK, GTK and 4th message of the Handshake.
        assert_eq!(updates.len(), 3);

        // Verify updates.
        let mut rxed_msg4 = false;
        let mut rxed_ptk = false;
        let mut rxed_gtk = false;
        for update in updates {
            match update {
                SecAssocUpdate::TxEapolKeyFrame(msg4) => {
                    // Verify fourth message.
                    rxed_msg4 = true;
                    assert_eq!(msg4.version, 1);
                    assert_eq!(msg4.packet_type, 3);
                    assert_eq!(msg4.packet_body_len as usize, msg4.len() - 4);
                    assert_eq!(msg4.descriptor_type, 2);
                    assert_eq!(msg4.key_info.value(), 0x030A);
                    assert_eq!(msg4.key_len, 0);
                    assert_eq!(msg4.key_replay_counter, key_replay_counter);
                    assert!(test_util::is_zero(&msg4.key_nonce[..]));
                    assert!(test_util::is_zero(&msg4.key_iv[..]));
                    assert_eq!(msg4.key_rsc, 0);
                    assert!(!test_util::is_zero(&msg4.key_mic[..]));
                    assert_eq!(msg4.key_mic.len(), test_util::mic_len());
                    assert_eq!(msg4.key_data.len(), 0);
                    assert!(test_util::is_zero(&msg4.key_data[..]));

                    // Verify the message's MIC.
                    let mic = test_util::compute_mic(ptk.kck(), &msg4);
                    assert_eq!(&msg4.key_mic[..], &mic[..]);
                }
                SecAssocUpdate::Key(Key::Ptk(reported_ptk)) => {
                    // Verify reported PTK is correct.
                    rxed_ptk = true;
                    assert_eq!(ptk.ptk(), reported_ptk.ptk());
                }
                SecAssocUpdate::Key(Key::Gtk(reported_gtk)) => {
                    // Verify reported GTK is correct.
                    rxed_gtk = true;
                    assert_eq!(&gtk[..], reported_gtk.gtk());
                }
                _ => assert!(false),
            }
        }

        assert!(rxed_msg4, "Supplicant didn't sent Handshake's 4th message");
        assert!(rxed_ptk, "Supplicant didn't sent PTK");
        assert!(rxed_gtk, "Supplicant didn't sent GTK");
    }

    // TODO(hahnr): Add additional tests to validate replay attacks,
    // invalid messages from Authenticator, timeouts, nonce reuse,
    // (in)-compatible protocol and RSNE versions, etc.
}
