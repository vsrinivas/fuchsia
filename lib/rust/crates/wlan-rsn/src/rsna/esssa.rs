// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use auth;
use eapol;
use failure;
use key::exchange::{self, Key};
use key::{gtk::Gtk, ptk::Ptk};
use rsna::{Role, SecAssocResult, SecAssocStatus, SecAssocUpdate};
use rsne::Rsne;
use std::mem;

#[derive(Debug)]
struct Pmksa {
    method: auth::Method,
    pmk: Option<Vec<u8>>,
}

impl Pmksa {
    fn reset(&mut self) {
        self.pmk = None;
    }
}

#[derive(Debug)]
enum Ptksa {
    Uninitialized(Option<exchange::Config>),
    Initialized(PtksaCfg),
}

impl Ptksa {
    fn initialize(&mut self, pmk: Vec<u8>) -> Result<(), failure::Error> {
        let cfg = match self {
            Ptksa::Uninitialized(cfg) => cfg.take(),
            _ => None,
        };
        let cfg = cfg.expect("invalid state: PTK configuration cannot be None");
        *self = Ptksa::Initialized(PtksaCfg {
            cfg: Some(cfg.clone()),
            method: exchange::Method::from_config(cfg, pmk)?,
            ptk: None,
        });
        Ok(())
    }

    fn reset(&mut self) {
        *self = Ptksa::Uninitialized(match self {
            Ptksa::Uninitialized(cfg) => cfg.take(),
            Ptksa::Initialized(PtksaCfg{cfg, ..}) => cfg.take(),
        });
    }

    fn is_established(&self) -> bool {
        match self {
            Ptksa::Initialized(PtksaCfg{ ptk: Some(_) , ..}) => true,
            _ => false,
        }
    }

    pub fn by_mut_ref(&mut self) -> &mut Self {
        self
    }
}

#[derive(Debug)]
struct PtksaCfg {
    cfg: Option<exchange::Config>,
    method: exchange::Method,
    ptk: Option<Ptk>,
}

#[derive(Debug)]
enum Gtksa {
    Uninitialized(Option<exchange::Config>),
    Initialized(GtksaCfg),
}

#[derive(Debug)]
struct GtksaCfg {
    cfg: Option<exchange::Config>,
    method: exchange::Method,
    gtk: Option<Gtk>,
}

impl Gtksa {
    fn initialize(&mut self, ptk: Vec<u8>) -> Result<(), failure::Error> {
        let cfg = match self {
            Gtksa::Uninitialized(cfg) => cfg.take(),
            _ => None,
        };
        let cfg = cfg.expect("invalid state: GTK configuration cannot be None");
        *self = Gtksa::Initialized(GtksaCfg {
            cfg: Some(cfg.clone()),
            method: exchange::Method::from_config(cfg, ptk)?,
            gtk: None,
        });
        Ok(())
    }

    fn reset(&mut self) {
        *self = Gtksa::Uninitialized(match self {
            Gtksa::Uninitialized(cfg) => cfg.take(),
            Gtksa::Initialized(GtksaCfg{cfg, ..}) => cfg.take(),
        });
    }

    fn is_established(&self) -> bool {
        match self {
            Gtksa::Initialized(GtksaCfg{ gtk: Some(_) , ..}) => true,
            _ => false,
        }
    }
}

// IEEE Std 802.11-2016, 12.6.1.3.2
#[derive(Debug)]
pub struct EssSa {
    // Configuration.
    role: Role,

    // Security associations.
    pmksa: Pmksa,
    ptksa: Ptksa,
    gtksa: Gtksa,
}

impl EssSa {
    pub fn new(
        role: Role,
        auth_cfg: auth::Config,
        ptk_exch_cfg: exchange::Config,
        gtk_exch_cfg: exchange::Config,
    ) -> Result<EssSa, failure::Error> {
        let auth_method = auth::Method::from_config(auth_cfg)?;

        let mut rsna = EssSa {
            role,
            pmksa: Pmksa {
                method: auth_method,
                pmk: None,
            },
            ptksa: Ptksa::Uninitialized(Some(ptk_exch_cfg)),
            gtksa: Gtksa::Uninitialized(Some(gtk_exch_cfg)),
        };
        rsna.init_pmksa()?;
        Ok(rsna)
    }

    pub fn reset(&mut self) {
        self.pmksa.reset();
        self.ptksa.reset();
    }

    fn is_established(&self) -> bool {
        self.ptksa.is_established() && self.gtksa.is_established()
    }

    fn on_key_confirmed(&mut self, key: Key) -> Result<(), failure::Error> {
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.pmk = Some(pmk);
                self.init_ptksa()
            }
            Key::Ptk(ptk) => {
                // The PTK carries KEK and KCK which is used in the Group Key Handshake, thus,
                // reset GTKSA whenever the PTK changed.
                self.gtksa.reset();
                self.gtksa.initialize(ptk.ptk().to_vec());

                if let Ptksa::Initialized(ptksa) = &mut self.ptksa {
                    ptksa.ptk = Some(ptk);
                }
                Ok(())
            }
            Key::Gtk(gtk) => {
                if let Gtksa::Initialized(gtksa) = &mut self.gtksa {
                    gtksa.gtk = Some(gtk);
                }
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
        let mut updates = match frame {
            &eapol::Frame::Key(ref key_frame) => self.on_eapol_key_frame(&key_frame),
            _ => return Ok(vec![]),
        }?;

        // Track keys to correctly update corresponding security associations.
        let was_esssa_established = self.is_established();
        for update in &updates {
            if let SecAssocUpdate::Key(key) = update {
                self.on_key_confirmed(key.clone())?;
            }
        }

        // Report if ESSSA was established successfully.
        if !was_esssa_established && self.is_established() {
            updates.push(SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));
        }

        Ok(updates)
    }

    fn on_eapol_key_frame(&mut self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.pmk {
            None => self.pmksa.method.on_eapol_key_frame(frame),
            Some(_) => match (&mut self.ptksa, &mut self.gtksa) {
                (Ptksa::Uninitialized(_), _) => Ok(vec![]),
                (Ptksa::Initialized(ptksa), Gtksa::Uninitialized(_)) => {
                    ptksa.method.on_eapol_key_frame(frame)
                },
                (Ptksa::Initialized(ptksa), Gtksa::Initialized(gtksa)) => {
                    // IEEE Std 802.11-2016, 12.7.2 b.2)
                    if frame.key_info.key_type() == eapol::KEY_TYPE_PAIRWISE {
                        ptksa.method.on_eapol_key_frame(frame)
                    } else if frame.key_info.key_type() == eapol::KEY_TYPE_GROUP_SMK {
                        gtksa.method.on_eapol_key_frame(frame)
                    } else {
                        eprintln!("unsupported EAPOL Key frame key type: {:?}",
                                  frame.key_info.key_type());
                        Ok(vec![])
                    }
                },
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
        let msg1 = eapol::Frame::Key(test_util::get_4whs_msg1(&anonce[..], |_| {}));
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
                let s_rsne = test_util::get_s_rsne();
                let s_rsne_data = test_util::get_rsne_bytes(&s_rsne);

                assert_eq!(msg2.version, 1);
                assert_eq!(msg2.packet_type, 3);
                assert_eq!(msg2.packet_body_len as usize, msg2.len() - 4);
                assert_eq!(msg2.descriptor_type, 2);
                assert_eq!(msg2.key_info.value(), 0x010A);
                assert_eq!(msg2.key_len, 0);
                assert_eq!(msg2.key_replay_counter, 1);
                assert!(!test_util::is_zero(&msg2.key_nonce[..]));
                assert!(test_util::is_zero(&msg2.key_iv[..]));
                assert_eq!(msg2.key_rsc, 0);
                assert!(!test_util::is_zero(&msg2.key_mic[..]));
                assert_eq!(msg2.key_mic.len(), test_util::mic_len());
                assert_eq!(msg2.key_data.len(), msg2.key_data_len as usize);
                assert_eq!(msg2.key_data.len(), s_rsne_data.len());
                assert_eq!(&msg2.key_data[..], &s_rsne_data[..]);

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
        let gtk = vec![42u8; 16];
        let key_frame = test_util::get_4whs_msg3(&ptk, &anonce[..], &gtk[..], |_| {});
        let msg3 = eapol::Frame::Key(key_frame);
        let updates = esssa
            .on_eapol_frame(&msg3)
            .expect("error processing third 4-Way Handshake method");

        // Expecting three updates: PTK, GTK and 4th message of the Handshake.
        assert_eq!(updates.len(), 4);

        // Verify updates.
        let mut rxed_msg4 = false;
        let mut rxed_ptk = false;
        let mut rxed_gtk = false;
        let mut rxed_esssa_established = false;
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
                    assert_eq!(msg4.key_replay_counter, 2);
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
                SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished) => {
                    rxed_esssa_established = true;
                }
                _ => assert!(false),
            }
        }

        assert!(rxed_msg4, "Supplicant didn't sent Handshake's 4th message");
        assert!(rxed_ptk, "Supplicant didn't sent PTK");
        assert!(rxed_gtk, "Supplicant didn't sent GTK");
        assert!(rxed_esssa_established, "Supplicant didn't sent ESSSA established message");
    }

    // TODO(hahnr): Add additional tests to validate replay attacks,
    // invalid messages from Authenticator, timeouts, nonce reuse,
    // (in)-compatible protocol and RSNE versions, etc.
}
