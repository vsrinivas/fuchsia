// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use akm::Akm;
use auth;
use bytes::Bytes;
use cipher::{Cipher, GROUP_CIPHER_SUITE, TKIP};
use eapol;
use Error;
use failure;
use key::exchange::{self, Key};
use key::{gtk::Gtk, ptk::Ptk};
use rsna::{NegotiatedRsne, Role, SecAssocResult, SecAssocStatus, SecAssocUpdate, VerifiedKeyFrame};
use rsne::Rsne;
use std::mem;

#[derive(Debug, PartialEq)]
struct Pmksa {
    method: auth::Method,
    pmk: Option<Vec<u8>>,
}

impl Pmksa {
    fn reset(&mut self) {
        self.pmk = None;
    }
}

#[derive(Debug, PartialEq)]
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

    fn ptk(&self) -> Option<&Ptk> {
        match self {
            Ptksa::Initialized(PtksaCfg{ ptk: Some(ptk) , ..}) => Some(ptk),
            _ => None,
        }
    }
}

#[derive(Debug, PartialEq)]
struct PtksaCfg {
    cfg: Option<exchange::Config>,
    method: exchange::Method,
    ptk: Option<Ptk>,
}

#[derive(Debug, PartialEq)]
enum Gtksa {
    Uninitialized(Option<exchange::Config>),
    Initialized(GtksaCfg),
}

#[derive(Debug, PartialEq)]
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

    fn gtk(&self) -> Option<&Gtk> {
        match self {
            Gtksa::Initialized(GtksaCfg{ gtk: Some(gtk) , ..}) => Some(gtk),
            _ => None,
        }
    }
}

// IEEE Std 802.11-2016, 12.6.1.3.2
#[derive(Debug, PartialEq)]
pub struct EssSa {
    // Configuration.
    role: Role,
    negotiated_rsne: NegotiatedRsne,
    key_replay_counter: u64,

    // Security associations.
    pmksa: Pmksa,
    ptksa: Ptksa,
    gtksa: Gtksa,
}

impl EssSa {
    pub fn new(
        role: Role,
        negotiated_rsne: NegotiatedRsne,
        auth_cfg: auth::Config,
        ptk_exch_cfg: exchange::Config,
        gtk_exch_cfg: exchange::Config,
    ) -> Result<EssSa, failure::Error> {
        let auth_method = auth::Method::from_config(auth_cfg)?;

        let mut rsna = EssSa {
            role,
            negotiated_rsne,
            key_replay_counter: 0,
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
        match (self.ptksa.ptk(), self.gtksa.gtk()) {
            (Some(_), Some(_)) => true,
            _ => false,
        }
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
            eapol::Frame::Key(key_frame) => self.on_eapol_key_frame(&key_frame),
            _ => return Ok(vec![]),
        }?;

        // Process Key updates ourselves to correctly track security associations.
        // If ESS-SA was not already established, wait with reporting PTK until GTK
        // is also known.
        let was_esssa_established = self.is_established();
        updates.drain_filter(|update| match update {
            SecAssocUpdate::Key(_) if !was_esssa_established => true,
            _ => false,
        })
        .for_each(|update| {
            if let SecAssocUpdate::Key(key) = update {
                self.on_key_confirmed(key.clone());
            }
        });

        // Report if ESSSA was established successfully for the first time,
        // as well as PTK and GTK.
        if !was_esssa_established {
            if let (Some(ptk), Some(gtk)) = (self.ptksa.ptk(), self.gtksa.gtk()) {
                updates.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                updates.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
                updates.push(SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));
            }
        }

        Ok(updates)
    }

    fn on_eapol_key_frame(&mut self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // Verify the frame complies with IEEE Std 802.11-2016, 12.7.2.
        let result = VerifiedKeyFrame::from_key_frame(frame, &self.role, &self.negotiated_rsne,
                                                      self.key_replay_counter,
                                                      self.ptksa.ptk(), self.gtksa.gtk());
        let verified_frame = match result {
            Err(Error::WrongAesKeywrapKey) => {
                return Ok(vec![SecAssocUpdate::Status(SecAssocStatus::WrongPassword)])
            }
            other => other,
        }?;

        // IEEE Std 802.11-2016, 12.7.2, d)
        // Update key replay counter if MIC was set and valid.
        if frame.key_info.key_mic() {
            self.key_replay_counter = frame.key_replay_counter;
        }

        // Forward frame to correct security association.
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.pmk {
            None => self.pmksa.method.on_eapol_key_frame(verified_frame),
            Some(_) => match (&mut self.ptksa, &mut self.gtksa) {
                (Ptksa::Uninitialized(_), _) => Ok(vec![]),
                (Ptksa::Initialized(ptksa), Gtksa::Uninitialized(_)) => {
                    ptksa.method.on_eapol_key_frame(verified_frame)
                },
                (Ptksa::Initialized(ptksa), Gtksa::Initialized(gtksa)) => {
                    // IEEE Std 802.11-2016, 12.7.2 b.2)
                    if frame.key_info.key_type() == eapol::KEY_TYPE_PAIRWISE {
                        ptksa.method.on_eapol_key_frame(verified_frame)
                    } else if frame.key_info.key_type() == eapol::KEY_TYPE_GROUP_SMK {
                        gtksa.method.on_eapol_key_frame(verified_frame)
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

    const ANONCE: [u8; 32] = [0x1A; 32];
    const GTK: [u8; 16] = [0x1B; 16];

    #[test]
    fn test_zero_key_replay_counter_msg1() {
        let mut esssa = test_util::get_esssa();

        let result = send_msg1(&mut esssa, |msg1| {
            msg1.key_replay_counter = 0;
        });
        let updates = result.expect("Supplicant failed processing 1st message");
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 2nd message");
        let ptk = extract_ptk(msg2);

        send_msg3(&mut esssa, &ptk, |_| {})
            .expect("Supplicant failed processing 3rd message");
    }

    #[test]
    fn test_nonzero_key_replay_counter_msg1() {
        let mut esssa = test_util::get_esssa();

        send_msg1(&mut esssa, |msg1| {
            msg1.key_replay_counter = 1;
        }).expect("Supplicant failed processing 1st message");
    }

    #[test]
    fn test_zero_key_replay_counter_lower_msg3_counter() {
        let mut esssa = test_util::get_esssa();

        let updates = send_msg1(&mut esssa, |msg1| {
            msg1.key_replay_counter = 1;
        }).expect("Supplicant failed processing 1st message");
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 2nd message");
        let ptk = extract_ptk(msg2);

        send_msg3(&mut esssa, &ptk, |msg3| {
            msg3.key_replay_counter = 0;
        }).expect("Supplicant failed processing 3rd message");
    }

    #[test]
    fn test_zero_key_replay_counter_valid_msg3() {
        let mut esssa = test_util::get_esssa();

        let updates = send_msg1(&mut esssa, |msg1| {
            msg1.key_replay_counter = 0;
        }).expect("Supplicant failed processing 1st message");
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 2nd message");
        let ptk = extract_ptk(msg2);

        send_msg3(&mut esssa, &ptk, |msg3| {
            msg3.key_replay_counter = 1;
        }).expect("Supplicant failed processing 3rd message");
    }

    #[test]
    fn test_zero_key_replay_counter_replayed_msg3() {
        let mut esssa = test_util::get_esssa();

        let updates = send_msg1(&mut esssa, |msg1| {
            msg1.key_replay_counter = 0;
        }).expect("Supplicant failed processing 1st message");
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 2nd message");
        let ptk = extract_ptk(msg2);

        send_msg3(&mut esssa, &ptk, |msg3| {
            msg3.key_replay_counter = 2;
        }).expect("Supplicant failed processing 3rd message");

        // The just sent third message increased the key replay counter.
        // All successive EAPOL frames are required to have a larger key replay counter.

        // Send an invalid message.
        send_msg3(&mut esssa, &ptk, |msg3| {
            msg3.key_replay_counter = 2;
        }).expect_err("Supplicant should have failed processing second, invalid, 3rd message");

        // Send a valid message.
        send_msg3(&mut esssa, &ptk, |msg3| {
            msg3.key_replay_counter = 3;
        }).expect("Supplicant failed processing third, valid, 3rd message");
    }

    // Integration test for WPA2 CCMP-128 PSK with a Supplicant role.
    #[test]
    fn test_supplicant_wpa2_ccmp128_psk() {
        // Create ESS Security Association
        let mut esssa = test_util::get_esssa();

        // Send first message
        let result = send_msg1(&mut esssa, |_| {});
        let updates = result.expect("Supplicant failed processing 1st message");

        // Verify 2nd message.
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 3nd message");
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

        // Send 3rd message.
        let ptk = extract_ptk(msg2);
        let updates = send_msg3(&mut esssa, &ptk, |_| {})
            .expect("Supplicant failed processing 3rd message");

        // Verify 4th message was received and is correct.
        let msg4 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 4th message");
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

        // Verify PTK was reported.
        let reported_ptk = extract_reported_ptk(&updates[..])
            .expect("Supplicant did not report PTK");
        assert_eq!(ptk.ptk(), reported_ptk.ptk());

        // Verify GTK was reported.
        let reported_gtk = extract_reported_gtk(&updates[..])
            .expect("Supplicant did not report GTK");
        assert_eq!(&GTK[..], reported_gtk.gtk());

        // Verify ESS was reported to be established.
        let reported_status = extract_reported_status(&updates[..])
            .expect("Supplicant did not report any status");
        match reported_status {
            SecAssocStatus::EssSaEstablished => {},
            _ => assert!(false),
        };
    }

    // TODO(hahnr): Add additional tests to validate replay attacks,
    // invalid messages from Authenticator, timeouts, nonce reuse,
    // (in)-compatible protocol and RSNE versions, etc.

    fn extract_ptk(msg2: &eapol::KeyFrame) -> Ptk {
        let snonce = msg2.key_nonce;
        test_util::get_ptk(&ANONCE[..], &snonce[..])
    }

    fn extract_eapol_resp(updates: &[SecAssocUpdate]) -> Option<&eapol::KeyFrame> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::TxEapolKeyFrame(resp) => Some(resp),
            _ => None,
        }).next()
    }

    fn extract_reported_ptk(updates: &[SecAssocUpdate]) -> Option<&Ptk> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::Key(Key::Ptk(ptk)) => Some(ptk),
            _ => None,
        }).next()
    }

    fn extract_reported_gtk(updates: &[SecAssocUpdate]) -> Option<&Gtk> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::Key(Key::Gtk(gtk)) => Some(gtk),
            _ => None,
        }).next()
    }

    fn extract_reported_status(updates: &[SecAssocUpdate]) -> Option<&SecAssocStatus> {
        updates.iter().filter_map(|u| match u {
            SecAssocUpdate::Status(status) => Some(status),
            _ => None,
        }).next()
    }

    fn send_msg1<F>(esssa: &mut EssSa, msg_modifier: F) -> SecAssocResult
        where F: Fn(&mut eapol::KeyFrame)
    {
        let msg = test_util::get_4whs_msg1(&ANONCE[..], msg_modifier);
        esssa.on_eapol_frame(&eapol::Frame::Key(msg))
    }

    fn send_msg3<F>(esssa: &mut EssSa, ptk: &Ptk, msg_modifier: F) -> SecAssocResult
        where F: Fn(&mut eapol::KeyFrame)
    {
        let (msg, _) = test_util::get_4whs_msg3(ptk, &ANONCE[..], &GTK[..], msg_modifier);
        esssa.on_eapol_frame(&eapol::Frame::Key(msg))
    }
}
