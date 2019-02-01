// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth;
use crate::key::exchange::{
    self,
    handshake::{fourway::Fourway, group_key::GroupKey},
    Key,
};
use crate::key::{gtk::Gtk, ptk::Ptk};
use crate::rsna::{
    NegotiatedRsne, Role, SecAssocStatus, SecAssocUpdate, UpdateSink, VerifiedKeyFrame,
};
use crate::state_machine::StateMachine;
use crate::Error;
use eapol;
use failure::{self, bail};
use log::{error, info};

#[derive(Debug, PartialEq)]
enum Pmksa {
    Initialized { method: auth::Method },
    Established { pmk: Vec<u8>, method: auth::Method },
}

impl Pmksa {
    fn reset(self) -> Self {
        match self {
            Pmksa::Established { method, .. } | Pmksa::Initialized { method, .. } => {
                Pmksa::Initialized { method }
            }
        }
    }
}

#[derive(Debug, PartialEq)]
enum Ptksa {
    Uninitialized { cfg: exchange::Config },
    Initialized { method: exchange::Method },
    Established { method: exchange::Method, ptk: Ptk },
}

impl Ptksa {
    fn initialize(self, pmk: Vec<u8>) -> Self {
        match self {
            Ptksa::Uninitialized { cfg } => match cfg {
                exchange::Config::FourWayHandshake(method_cfg) => {
                    match Fourway::new(method_cfg.clone(), pmk) {
                        Err(e) => {
                            error!("error creating 4-Way Handshake from config: {}", e);
                            Ptksa::Uninitialized {
                                cfg: exchange::Config::FourWayHandshake(method_cfg),
                            }
                        }
                        Ok(method) => Ptksa::Initialized {
                            method: exchange::Method::FourWayHandshake(method),
                        },
                    }
                }
                _ => {
                    panic!("unsupported method for PTKSA: {:?}", cfg);
                }
            },
            other => other,
        }
    }

    fn reset(self) -> Self {
        match self {
            Ptksa::Uninitialized { cfg } => Ptksa::Uninitialized { cfg },
            Ptksa::Initialized { method } | Ptksa::Established { method, .. } => {
                Ptksa::Uninitialized { cfg: method.destroy() }
            }
        }
    }
}

/// A GTKSA is composed of a GTK and a key exchange method.
/// While a key is required for successfully establishing a GTKSA, the key exchange method is
/// optional as it's used only for re-keying the GTK.
#[derive(Debug, PartialEq)]
enum Gtksa {
    Uninitialized { cfg: Option<exchange::Config> },
    Initialized { method: Option<exchange::Method> },
    Established { method: Option<exchange::Method>, gtk: Gtk },
}

impl Gtksa {
    fn initialize(self, kck: &[u8], kek: &[u8]) -> Self {
        match self {
            Gtksa::Uninitialized { cfg } => match cfg {
                None => Gtksa::Initialized { method: None },
                Some(exchange::Config::GroupKeyHandshake(method_cfg)) => {
                    match GroupKey::new(method_cfg.clone(), kck, kek) {
                        Err(e) => {
                            error!("error creating Group KeyHandshake from config: {}", e);
                            Gtksa::Uninitialized {
                                cfg: Some(exchange::Config::GroupKeyHandshake(method_cfg)),
                            }
                        }
                        Ok(method) => Gtksa::Initialized {
                            method: Some(exchange::Method::GroupKeyHandshake(method)),
                        },
                    }
                }
                _ => {
                    panic!("unsupported method for GTKSA: {:?}", cfg);
                }
            },
            other => other,
        }
    }

    fn reset(self) -> Self {
        match self {
            Gtksa::Uninitialized { cfg } => Gtksa::Uninitialized { cfg },
            Gtksa::Initialized { method } | Gtksa::Established { method, .. } => {
                Gtksa::Uninitialized { cfg: method.map(|m| m.destroy()) }
            }
        }
    }
}

/// An ESS Security Association is composed of three security associations, namely, PMKSA, PTKSA and
/// GTKSA. The individual security associations have dependencies on each other. For example, the
/// PMKSA must be established first as it yields the PMK used in the PTK and GTK key hierarchy.
/// Depending on the selected PTKSA, it can yield not just the PTK but also GTK, and thus leaving
/// the GTKSA's key exchange method only useful for re-keying.
#[derive(Debug, PartialEq)]
pub(crate) struct EssSa {
    // Configuration.
    role: Role,
    pub negotiated_rsne: NegotiatedRsne,
    key_replay_counter: u64,

    // Security associations.
    pmksa: StateMachine<Pmksa>,
    ptksa: StateMachine<Ptksa>,
    gtksa: StateMachine<Gtksa>,
}

// IEEE Std 802.11-2016, 12.6.1.3.2
impl EssSa {
    pub fn new(
        role: Role,
        negotiated_rsne: NegotiatedRsne,
        auth_cfg: auth::Config,
        ptk_exch_cfg: exchange::Config,
        gtk_exch_cfg: Option<exchange::Config>,
    ) -> Result<EssSa, failure::Error> {
        info!("spawned ESSSA for: {:?}", role);

        let auth_method = auth::Method::from_config(auth_cfg)?;
        let rsna = EssSa {
            role,
            negotiated_rsne,
            key_replay_counter: 0,
            pmksa: StateMachine::new(Pmksa::Initialized { method: auth_method }),
            ptksa: StateMachine::new(Ptksa::Uninitialized { cfg: ptk_exch_cfg }),
            gtksa: StateMachine::new(Gtksa::Uninitialized { cfg: gtk_exch_cfg }),
        };
        Ok(rsna)
    }

    pub fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), failure::Error> {
        self.reset();
        info!("establishing ESSSA...");

        // PSK allows deriving the PMK without exchanging
        let pmk = match &self.pmksa.state() {
            Pmksa::Initialized { method } => match method {
                auth::Method::Psk(psk) => psk.to_vec(),
            },
            _ => bail!("cannot initiate PMK more than once"),
        };
        self.on_key_confirmed(update_sink, Key::Pmk(pmk))?;

        // TODO(hahnr): Support 802.1X authentication if STA is Authenticator and authentication
        // method is not PSK.

        Ok(())
    }

    pub fn reset(&mut self) {
        info!("resetting ESSSA");
        self.pmksa.replace_state(|state| state.reset());
        self.ptksa.replace_state(|state| state.reset());
        self.gtksa.replace_state(|state| state.reset());
    }

    fn is_established(&self) -> bool {
        match (self.ptksa.state(), self.gtksa.state()) {
            (Ptksa::Established { .. }, Gtksa::Established { .. }) => true,
            _ => false,
        }
    }

    fn on_key_confirmed(
        &mut self,
        update_sink: &mut UpdateSink,
        key: Key,
    ) -> Result<(), failure::Error> {
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.replace_state(|state| match state {
                    Pmksa::Initialized { method } => {
                        info!("established PMKSA");
                        Pmksa::Established { method, pmk: pmk.clone() }
                    }
                    other => {
                        error!("received PMK with PMK already being established");
                        other
                    }
                });

                self.ptksa.replace_state(|state| state.initialize(pmk));
                if let Ptksa::Initialized { method } = self.ptksa.mut_state() {
                    method.initiate(update_sink, self.key_replay_counter)?;
                } else {
                    bail!("PTKSA not initialized");
                }
            }
            Key::Ptk(ptk) => {
                // The PTK carries KEK and KCK which is used in the Group Key Handshake, thus,
                // reset GTKSA whenever the PTK changed.
                self.gtksa.replace_state(|state| state.reset().initialize(ptk.kck(), ptk.kek()));

                self.ptksa.replace_state(|state| match state {
                    Ptksa::Initialized { method } => {
                        info!("established PTKSA");
                        Ptksa::Established { method, ptk }
                    }
                    Ptksa::Established { method, .. } => {
                        // PTK was already initialized.
                        info!("re-established new PTKSA; invalidating previous one");
                        info!("(this is likely a result of using a wrong password)");
                        Ptksa::Established { method, ptk }
                    }
                    other @ Ptksa::Uninitialized { .. } => {
                        error!("received PTK in unexpected PTKSA state");
                        other
                    }
                });
            }
            Key::Gtk(gtk) => {
                self.gtksa.replace_state(|state| match state {
                    Gtksa::Initialized { method } => {
                        info!("established GTKSA");
                        Gtksa::Established { method, gtk }
                    }
                    Gtksa::Established { method, .. } => {
                        info!("re-established new GTKSA; invalidating previous one");
                        Gtksa::Established { method, gtk }
                    }
                    Gtksa::Uninitialized { cfg } => {
                        error!("received GTK in unexpected GTKSA state");
                        Gtksa::Uninitialized { cfg }
                    }
                });
            }
            _ => {}
        };
        Ok(())
    }

    pub fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::Frame,
    ) -> Result<(), failure::Error> {
        // Only processes EAPOL Key frames. Drop all other frames silently.
        let mut updates = match frame {
            eapol::Frame::Key(key_frame) => {
                let mut updates = UpdateSink::default();
                self.on_eapol_key_frame(&mut updates, &key_frame)?;

                // Authenticator updates its key replay counter with every outbound EAPOL frame.
                if let Role::Authenticator = self.role {
                    for update in &updates {
                        if let SecAssocUpdate::TxEapolKeyFrame(frame) = update {
                            if frame.key_replay_counter <= self.key_replay_counter {
                                error!("tx EAPOL Key frame uses invalid key replay counter: {:?} ({:?})",
                                          frame.key_replay_counter,
                                          self.key_replay_counter);
                            }
                            self.key_replay_counter = frame.key_replay_counter;
                        }
                    }
                }

                updates
            }
        };

        // Process Key updates ourselves to correctly track security associations.
        // If ESS-SA was not already established, wait with reporting PTK until GTK
        // is also known.
        let was_esssa_established = self.is_established();
        updates
            .drain_filter(|update| match update {
                SecAssocUpdate::Key(_) => true,
                _ => false,
            })
            .for_each(|update| {
                if let SecAssocUpdate::Key(key) = update {
                    if let Err(e) = self.on_key_confirmed(update_sink, key.clone()) {
                        error!("error while processing key: {}", e);
                    };
                }
            });
        update_sink.append(&mut updates);

        // Report keys once an ESSSA is established.
        let state = (self.ptksa.state(), self.gtksa.state());
        if let (Ptksa::Established { ptk, .. }, Gtksa::Established { gtk, .. }) = state {
            if !was_esssa_established {
                info!("established ESSSA");
                update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
                update_sink.push(SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));
            } else {
                info!("rekey'ed some keys of an established ESSSA");
                update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk.clone())));
            }
        }

        Ok(())
    }

    fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::KeyFrame,
    ) -> Result<(), failure::Error> {
        // Verify the frame complies with IEEE Std 802.11-2016, 12.7.2.
        let result = VerifiedKeyFrame::from_key_frame(
            frame,
            &self.role,
            &self.negotiated_rsne,
            self.key_replay_counter,
        );
        // TODO(hahnr): The status should not be pushed as an update but isntead as a Result.
        let verified_frame = match result {
            Err(e) => match e.as_fail().downcast_ref::<Error>() {
                Some(Error::WrongAesKeywrapKey) => {
                    update_sink.push(SecAssocUpdate::Status(SecAssocStatus::WrongPassword));
                    return Ok(());
                }
                _ => bail!(e),
            },
            other => other,
        }?;

        // IEEE Std 802.11-2016, 12.7.2, d)
        // Update key replay counter if MIC was set and is valid. Only applicable for Supplicant.
        // TODO(hahnr): We should verify the MIC here and only increase the counter if the MIC
        // is valid.
        if frame.key_info.key_mic() {
            if let Role::Supplicant = self.role {
                self.key_replay_counter = frame.key_replay_counter;
            }
        }

        // Forward frame to correct security association.
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.mut_state() {
            Pmksa::Initialized { method } => {
                return method.on_eapol_key_frame(update_sink, verified_frame);
            }
            Pmksa::Established { .. } => {}
        };

        // Once PMKSA was established PTKSA and GTKSA can process frames.
        // IEEE Std 802.11-2016, 12.7.2 b.2)
        if frame.key_info.key_type() == eapol::KEY_TYPE_PAIRWISE {
            match self.ptksa.mut_state() {
                Ptksa::Uninitialized { .. } => Ok(()),
                Ptksa::Initialized { method } | Ptksa::Established { method, .. } => {
                    method.on_eapol_key_frame(update_sink, self.key_replay_counter, verified_frame)
                }
            }
        } else if frame.key_info.key_type() == eapol::KEY_TYPE_GROUP_SMK {
            match self.gtksa.mut_state() {
                Gtksa::Uninitialized { .. } => Ok(()),
                Gtksa::Initialized { method } | Gtksa::Established { method, .. } => match method {
                    Some(method) => method.on_eapol_key_frame(
                        update_sink,
                        self.key_replay_counter,
                        verified_frame,
                    ),
                    None => {
                        error!("received group key EAPOL Key frame with GTK re-keying disabled");
                        Ok(())
                    }
                },
            }
        } else {
            error!("unsupported EAPOL Key frame key type: {:?}", frame.key_info.key_type());
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rsna::test_util;
    use crate::Supplicant;

    const ANONCE: [u8; 32] = [0x1A; 32];
    const GTK: [u8; 16] = [0x1B; 16];
    const GTK_REKEY: [u8; 16] = [0x1F; 16];

    #[test]
    fn test_supplicant_with_authenticator() {
        let mut supplicant = test_util::get_supplicant();
        let mut authenticator = test_util::get_authenticator();
        supplicant.start().expect("Failed starting Supplicant");

        // Initiate Authenticator.
        let mut a_updates = vec![];
        let result = authenticator.initiate(&mut a_updates);
        assert!(result.is_ok(), "Authenticator failed initiating: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 1);
        let msg1 = extract_eapol_resp(&a_updates).expect("Authenticator did not send msg #1");

        // Send msg #1 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, &eapol::Frame::Key(msg1.clone()));
        assert!(result.is_ok(), "Supplicant failed processing msg #1: {}", result.unwrap_err());
        assert_eq!(s_updates.len(), 1);
        let msg2 = extract_eapol_resp(&s_updates).expect("Supplicant did not send msg #2");

        // Send msg #2 to Authenticator and wait for response.
        let mut a_updates = vec![];
        let result = authenticator.on_eapol_frame(&mut a_updates, &eapol::Frame::Key(msg2.clone()));
        assert!(result.is_ok(), "Authenticator failed processing msg #2: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 1);
        let msg3 = extract_eapol_resp(&a_updates).expect("Authenticator did not send msg #3");

        // Send msg #3 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, &eapol::Frame::Key(msg3.clone()));
        assert!(result.is_ok(), "Supplicant failed processing msg #3: {}", result.unwrap_err());
        assert_eq!(s_updates.len(), 4);
        let msg4 = extract_eapol_resp(&s_updates).expect("Supplicant did not send msg #4");
        let s_ptk = extract_reported_ptk(&s_updates).expect("Supplicant did not send PTK");
        let s_gtk = extract_reported_gtk(&s_updates).expect("Supplicant did not send GTK");
        let s_status = extract_reported_status(&s_updates).expect("Supplicant did not send status");

        // Send msg #4 to Authenticator.
        let mut a_updates = vec![];
        let result = authenticator.on_eapol_frame(&mut a_updates, &eapol::Frame::Key(msg4.clone()));
        assert!(result.is_ok(), "Authenticator failed processing msg #4: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 3);
        let a_ptk = extract_reported_ptk(&a_updates).expect("Authenticator did not send PTK");
        let a_gtk = extract_reported_gtk(&a_updates).expect("Authenticator did not send GTK");
        let a_status =
            extract_reported_status(&a_updates).expect("Authenticator did not send status");

        // Verify derived keys match and status reports ESS-SA as established.
        assert_eq!(a_ptk, s_ptk);
        assert_eq!(a_gtk, s_gtk);
        match (a_status, s_status) {
            (SecAssocStatus::EssSaEstablished, SecAssocStatus::EssSaEstablished) => {}
            (a, s) => panic!("Invalid status; Authenticator: {:?}, Supplicant: {:?}", a, s),
        };
    }
    #[test]
    fn test_replay_first_message() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message of handshake.
        let (result, _) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 1;
        });
        assert!(result.is_ok());

        // Replay first message which should restart the entire handshake.
        // Verify the second message of the handshake was received.
        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 3;
        });
        assert!(result.is_ok());

        // Extract second message response and verify Supplicant responded to the replayed first
        // message.
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        assert_eq!(msg2.key_replay_counter, 3);
    }

    #[test]
    fn test_zero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 0;
        });
        assert!(result.is_ok());
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let ptk = derive_ptk(msg2);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |_| {});
        assert!(result.is_ok());
    }

    #[test]
    fn test_nonzero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, _) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 1;
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_lower_msg3_counter() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 1;
        });
        assert!(result.is_ok());
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let ptk = derive_ptk(msg2);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_replay_counter = 0;
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_valid_msg3() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 0;
        });
        assert!(result.is_ok());
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let ptk = derive_ptk(msg2);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_replay_counter = 1;
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_replayed_msg3() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 0;
        });
        assert!(result.is_ok());
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let ptk = derive_ptk(msg2);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_replay_counter = 2;
        });
        assert!(result.is_ok());

        // The just sent third message increased the key replay counter.
        // All successive EAPOL frames are required to have a larger key replay counter.

        // Send an invalid message.
        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_replay_counter = 2;
        });
        assert!(result.is_err());

        // Send a valid message.
        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_replay_counter = 3;
        });
        assert!(result.is_ok());
    }

    // Replays the first message of the 4-Way Handshake to verify that
    // (1) the Supplicant discards the first derived PTK in favor of a new one, and
    // (2) the Supplicant is not reusing a nonce from its previous message,
    // (3) the Supplicant only reports a new PTK if the 4-Way Handshake was completed successfully.
    #[test]
    fn test_replayed_msg1_ptk_installation() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send 1st message of 4-Way Handshake for the first time and derive PTK.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 1;
        });
        assert_eq!(extract_reported_ptk(&updates[..]), None);
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let first_ptk = derive_ptk(msg2);
        let first_nonce = msg2.key_nonce;

        // Send 1st message of 4-Way Handshake a second time and derive PTK.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_replay_counter = 2;
        });
        assert_eq!(extract_reported_ptk(&updates[..]), None);
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 2nd message");
        let second_ptk = derive_ptk(msg2);
        let second_nonce = msg2.key_nonce;

        // Send 3rd message of 4-Way Handshake.
        // The Supplicant now finished the 4-Way Handshake and should report its PTK.
        let (_, updates) = send_msg3(&mut supplicant, &second_ptk, |msg3| {
            msg3.key_replay_counter = 3;
        });
        let installed_ptk =
            extract_reported_ptk(&updates[..]).expect("Supplicant did not report PTK");

        assert_ne!(first_nonce, second_nonce);
        assert_ne!(&first_ptk, &second_ptk);
        assert_eq!(installed_ptk, &second_ptk);
    }

    // Integration test for WPA2 CCMP-128 PSK with a Supplicant role.
    #[test]
    fn test_supplicant_wpa2_ccmp128_psk() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message
        let (result, updates) = send_msg1(&mut supplicant, |_| {});
        assert!(result.is_ok());

        // Verify 2nd message.
        let msg2 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 3nd message");
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
        let ptk = derive_ptk(msg2);
        let (result, updates) = send_msg3(&mut supplicant, &ptk, |_| {});
        assert!(result.is_ok());

        // Verify 4th message was received and is correct.
        let msg4 =
            extract_eapol_resp(&updates[..]).expect("Supplicant did not respond with 4th message");
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
        let reported_ptk =
            extract_reported_ptk(&updates[..]).expect("Supplicant did not report PTK");
        assert_eq!(ptk.ptk, reported_ptk.ptk);

        // Verify GTK was reported.
        let reported_gtk =
            extract_reported_gtk(&updates[..]).expect("Supplicant did not report GTK");
        assert_eq!(&GTK[..], &reported_gtk.gtk[..]);

        // Verify ESS was reported to be established.
        let reported_status =
            extract_reported_status(&updates[..]).expect("Supplicant did not report any status");
        match reported_status {
            SecAssocStatus::EssSaEstablished => {}
            _ => assert!(false),
        };

        // Cause re-keying of GTK via Group-Key Handshake.

        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, |_| {});
        assert!(result.is_ok());

        // Verify 2th message was received and is correct.
        let msg2 = extract_eapol_resp(&updates[..])
            .expect("Supplicant did not respond with 2nd message of group key handshake");
        assert_eq!(msg2.version, 1);
        assert_eq!(msg2.packet_type, 3);
        assert_eq!(msg2.packet_body_len as usize, msg2.len() - 4);
        assert_eq!(msg2.descriptor_type, 2);
        assert_eq!(msg2.key_info.value(), 0x0302);
        assert_eq!(msg2.key_len, 0);
        assert_eq!(msg2.key_replay_counter, 3);
        assert!(test_util::is_zero(&msg2.key_nonce[..]));
        assert!(test_util::is_zero(&msg2.key_iv[..]));
        assert_eq!(msg2.key_rsc, 0);
        assert!(!test_util::is_zero(&msg2.key_mic[..]));
        assert_eq!(msg2.key_mic.len(), test_util::mic_len());
        assert_eq!(msg2.key_data.len(), 0);
        assert!(test_util::is_zero(&msg2.key_data[..]));
        // Verify the message's MIC.
        let mic = test_util::compute_mic(ptk.kck(), &msg2);
        assert_eq!(&msg2.key_mic[..], &mic[..]);

        // Verify GTK was reported.
        let reported_gtk =
            extract_reported_gtk(&updates[..]).expect("Supplicant did not report re-key'ed GTK");
        assert_eq!(&GTK_REKEY[..], &reported_gtk.gtk[..]);
    }

    // TODO(hahnr): Add additional tests to validate replay attacks,
    // invalid messages from Authenticator, timeouts, nonce reuse,
    // (in)-compatible protocol and RSNE versions, etc.

    fn derive_ptk(msg2: &eapol::KeyFrame) -> Ptk {
        let snonce = msg2.key_nonce;
        test_util::get_ptk(&ANONCE[..], &snonce[..])
    }

    fn extract_eapol_resp(updates: &[SecAssocUpdate]) -> Option<&eapol::KeyFrame> {
        updates
            .iter()
            .filter_map(|u| match u {
                SecAssocUpdate::TxEapolKeyFrame(resp) => Some(resp),
                _ => None,
            })
            .next()
    }

    fn extract_reported_ptk(updates: &[SecAssocUpdate]) -> Option<&Ptk> {
        updates
            .iter()
            .filter_map(|u| match u {
                SecAssocUpdate::Key(Key::Ptk(ptk)) => Some(ptk),
                _ => None,
            })
            .next()
    }

    fn extract_reported_gtk(updates: &[SecAssocUpdate]) -> Option<&Gtk> {
        updates
            .iter()
            .filter_map(|u| match u {
                SecAssocUpdate::Key(Key::Gtk(gtk)) => Some(gtk),
                _ => None,
            })
            .next()
    }

    fn extract_reported_status(updates: &[SecAssocUpdate]) -> Option<&SecAssocStatus> {
        updates
            .iter()
            .filter_map(|u| match u {
                SecAssocUpdate::Status(status) => Some(status),
                _ => None,
            })
            .next()
    }

    fn send_msg1<F>(
        supplicant: &mut Supplicant,
        msg_modifier: F,
    ) -> (Result<(), failure::Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrame),
    {
        let msg = test_util::get_4whs_msg1(&ANONCE[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, &eapol::Frame::Key(msg));
        (result, update_sink)
    }

    fn send_msg3<F>(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        msg_modifier: F,
    ) -> (Result<(), failure::Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrame),
    {
        let msg = test_util::get_4whs_msg3(ptk, &ANONCE[..], &GTK[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, &eapol::Frame::Key(msg));
        (result, update_sink)
    }

    fn send_group_key_msg1<F>(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        msg_modifier: F,
    ) -> (Result<(), failure::Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrame),
    {
        let msg = test_util::get_group_key_hs_msg1(ptk, &GTK_REKEY[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, &eapol::Frame::Key(msg));
        (result, update_sink)
    }
}
