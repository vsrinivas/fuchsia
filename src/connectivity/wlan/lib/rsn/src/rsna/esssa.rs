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
    Dot11VerifiedKeyFrame, NegotiatedProtection, Role, SecAssocStatus, SecAssocUpdate, UpdateSink,
};
use anyhow::format_err;
use eapol;
use log::{error, info};
use std::collections::HashSet;
use wlan_statemachine::StateMachine;
use zerocopy::ByteSlice;

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
    Uninitialized {
        cfg: Option<exchange::Config>,
    },
    Initialized {
        method: Option<exchange::Method>,
    },
    Established {
        method: Option<exchange::Method>,
        // A key history of previously installed group keys.
        // Keys which have been previously installed must never be re-installed.
        installed_gtks: HashSet<Gtk>,
    },
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
///
/// Each association should spawn one ESSSA instance only.
/// The security association correctly tracks and handles replays for robustness and
/// prevents key re-installation to mitigate attacks such as described in KRACK.
#[derive(Debug, PartialEq)]
pub(crate) struct EssSa {
    // Determines the device's role (Supplicant or Authenticator).
    role: Role,
    // The protection used for this association.
    pub negotiated_protection: NegotiatedProtection,
    // The last valid key replay counter. Messages with a key replay counter lower than this counter
    // value will be dropped.
    key_replay_counter: u64,

    // Individual Security Associations.
    pmksa: StateMachine<Pmksa>,
    ptksa: StateMachine<Ptksa>,
    gtksa: StateMachine<Gtksa>,
}

// IEEE Std 802.11-2016, 12.6.1.3.2
impl EssSa {
    pub fn new(
        role: Role,
        negotiated_protection: NegotiatedProtection,
        auth_cfg: auth::Config,
        ptk_exch_cfg: exchange::Config,
        gtk_exch_cfg: Option<exchange::Config>,
    ) -> Result<EssSa, anyhow::Error> {
        info!("spawned ESSSA for: {:?}", role);

        let auth_method = auth::Method::from_config(auth_cfg)?;
        let rsna = EssSa {
            role,
            negotiated_protection,
            key_replay_counter: 0,
            pmksa: StateMachine::new(Pmksa::Initialized { method: auth_method }),
            ptksa: StateMachine::new(Ptksa::Uninitialized { cfg: ptk_exch_cfg }),
            gtksa: StateMachine::new(Gtksa::Uninitialized { cfg: gtk_exch_cfg }),
        };
        Ok(rsna)
    }

    pub fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), anyhow::Error> {
        self.reset();
        info!("establishing ESSSA...");

        // PSK allows deriving the PMK without exchanging
        let pmk = match self.pmksa.as_ref() {
            Pmksa::Initialized { method } => match method {
                auth::Method::Psk(psk) => psk.to_vec(),
            },
            _ => return Err(format_err!("cannot initiate PMK more than once")),
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
        match (self.ptksa.as_ref(), self.gtksa.as_ref()) {
            (Ptksa::Established { .. }, Gtksa::Established { .. }) => true,
            _ => false,
        }
    }

    fn on_key_confirmed(
        &mut self,
        update_sink: &mut UpdateSink,
        key: Key,
    ) -> Result<(), anyhow::Error> {
        let was_esssa_established = self.is_established();
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.replace_state(|state| match state {
                    Pmksa::Initialized { method } => {
                        info!("established PMKSA");
                        update_sink.push(SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished));
                        Pmksa::Established { method, pmk: pmk.clone() }
                    }
                    other => {
                        error!("received PMK with PMK already being established");
                        other
                    }
                });

                self.ptksa.replace_state(|state| state.initialize(pmk));
                if let Ptksa::Initialized { method } = self.ptksa.as_mut() {
                    method.initiate(update_sink, self.key_replay_counter)?;
                } else {
                    return Err(format_err!("PTKSA not initialized"));
                }
            }
            Key::Ptk(ptk) => {
                // The PTK carries KEK and KCK which is used in the Group Key Handshake, thus,
                // reset GTKSA whenever the PTK changed.
                self.gtksa.replace_state(|state| state.reset().initialize(ptk.kck(), ptk.kek()));

                self.ptksa.replace_state(|state| match state {
                    Ptksa::Initialized { method } => {
                        info!("established PTKSA");
                        update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                        Ptksa::Established { method, ptk }
                    }
                    Ptksa::Established { method, .. } => {
                        // PTK was already initialized.
                        info!("re-established new PTKSA; invalidating previous one");
                        info!("(this is likely a result of using a wrong password)");
                        // Key can be re-established in two cases:
                        // 1. Message gets replayed
                        // 2. Key is being rotated
                        // Checking that ESSSA is already established eliminates the first case.
                        if was_esssa_established {
                            update_sink.push(SecAssocUpdate::Key(Key::Ptk(ptk.clone())));
                        }
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

                        let mut installed_gtks = HashSet::default();
                        installed_gtks.insert(gtk.clone());
                        update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));
                        Gtksa::Established { method, installed_gtks }
                    }
                    Gtksa::Established { method, mut installed_gtks } => {
                        info!("re-established new GTKSA; invalidating previous one");

                        if !installed_gtks.contains(&gtk) {
                            installed_gtks.insert(gtk.clone());
                            update_sink.push(SecAssocUpdate::Key(Key::Gtk(gtk)));
                        }
                        Gtksa::Established { method, installed_gtks }
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

    pub fn on_eapol_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<B>,
    ) -> Result<(), anyhow::Error> {
        // Only processes EAPOL Key frames. Drop all other frames silently.
        let updates = match frame {
            eapol::Frame::Key(key_frame) => {
                let mut updates = UpdateSink::default();
                self.on_eapol_key_frame(&mut updates, key_frame)?;

                // Authenticator updates its key replay counter with every outbound EAPOL frame.
                if let Role::Authenticator = self.role {
                    for update in &updates {
                        if let SecAssocUpdate::TxEapolKeyFrame(eapol_frame) = update {
                            let krc = eapol_frame
                                .keyframe()
                                .key_frame_fields
                                .key_replay_counter
                                .to_native();

                            if krc <= self.key_replay_counter {
                                error!("tx EAPOL Key frame uses invalid key replay counter: {:?} ({:?})",
                                       krc,
                                          self.key_replay_counter);
                            }
                            self.key_replay_counter = krc;
                        }
                    }
                }

                updates
            }
            _ => UpdateSink::default(),
        };

        let was_esssa_established = self.is_established();
        for update in updates {
            match update {
                // Process Key updates ourselves to correctly track security associations.
                SecAssocUpdate::Key(key) => {
                    if let Err(e) = self.on_key_confirmed(update_sink, key) {
                        error!("error while processing key: {}", e);
                    };
                }
                // Forward all other updates.
                _ => update_sink.push(update),
            }
        }

        // Report once ESSSA is established.
        if !was_esssa_established && self.is_established() {
            info!("established ESSSA");
            update_sink.push(SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));
        }

        Ok(())
    }

    fn on_eapol_key_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::KeyFrameRx<B>,
    ) -> Result<(), anyhow::Error> {
        // Verify the frame complies with IEEE Std 802.11-2016, 12.7.2.
        let verified_frame = Dot11VerifiedKeyFrame::from_frame(
            frame,
            &self.role,
            &self.negotiated_protection,
            self.key_replay_counter,
        )?;

        // Safe: frame was just verified.
        let raw_frame = verified_frame.unsafe_get_raw();

        // IEEE Std 802.11-2016, 12.7.2, d)
        // Update key replay counter if MIC was set and is valid. Only applicable for Supplicant.
        // TODO(hahnr): We should verify the MIC here and only increase the counter if the MIC
        // is valid.
        if raw_frame.key_frame_fields.key_info().key_mic() {
            if let Role::Supplicant = self.role {
                self.key_replay_counter = raw_frame.key_frame_fields.key_replay_counter.to_native();
            }
        }

        // Forward frame to correct security association.
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.as_mut() {
            Pmksa::Initialized { method } => {
                return method.on_eapol_key_frame(update_sink, verified_frame);
            }
            Pmksa::Established { .. } => {}
        };

        // Once PMKSA was established PTKSA and GTKSA can process frames.
        // IEEE Std 802.11-2016, 12.7.2 b.2)
        if raw_frame.key_frame_fields.key_info().key_type() == eapol::KeyType::PAIRWISE {
            match self.ptksa.as_mut() {
                Ptksa::Uninitialized { .. } => Ok(()),
                Ptksa::Initialized { method } | Ptksa::Established { method, .. } => {
                    method.on_eapol_key_frame(update_sink, self.key_replay_counter, verified_frame)
                }
            }
        } else if raw_frame.key_frame_fields.key_info().key_type() == eapol::KeyType::GROUP_SMK {
            match self.gtksa.as_mut() {
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
            error!(
                "unsupported EAPOL Key frame key type: {:?}",
                raw_frame.key_frame_fields.key_info().key_type()
            );
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::key::exchange::compute_mic;
    use crate::rsna::test_util;
    use crate::rsna::test_util::expect_eapol_resp;
    use crate::Supplicant;

    const ANONCE: [u8; 32] = [0x1A; 32];
    const GTK: [u8; 16] = [0x1B; 16];
    const GTK_REKEY: [u8; 16] = [0x1F; 16];
    const GTK_REKEY_2: [u8; 16] = [0x2F; 16];

    #[test]
    fn test_supplicant_with_authenticator() {
        let mut supplicant = test_util::get_supplicant();
        let mut authenticator = test_util::get_authenticator();
        supplicant.start().expect("Failed starting Supplicant");

        // Initiate Authenticator.
        let mut a_updates = vec![];
        let result = authenticator.initiate(&mut a_updates);
        assert!(result.is_ok(), "Authenticator failed initiating: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 2);
        let msg1 = test_util::expect_eapol_resp(&a_updates[..]);
        let a_status = test_util::expect_reported_status(&a_updates);
        assert_eq!(a_status, SecAssocStatus::PmkSaEstablished);

        // Send msg #1 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, eapol::Frame::Key(msg1.keyframe()));
        assert!(result.is_ok(), "Supplicant failed processing msg #1: {}", result.unwrap_err());
        assert_eq!(s_updates.len(), 1);
        let msg2 = test_util::expect_eapol_resp(&s_updates[..]);

        // Send msg #2 to Authenticator and wait for response.
        let mut a_updates = vec![];
        let result =
            authenticator.on_eapol_frame(&mut a_updates, eapol::Frame::Key(msg2.keyframe()));
        assert!(result.is_ok(), "Authenticator failed processing msg #2: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 1);
        let msg3 = test_util::expect_eapol_resp(&a_updates[..]);

        // Send msg #3 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, eapol::Frame::Key(msg3.keyframe()));
        assert!(result.is_ok(), "Supplicant failed processing msg #3: {}", result.unwrap_err());
        assert_eq!(s_updates.len(), 4, "{:?}", s_updates);
        let msg4 = test_util::expect_eapol_resp(&s_updates[..]);
        let s_ptk = test_util::expect_reported_ptk(&s_updates[..]);
        let s_gtk = test_util::expect_reported_gtk(&s_updates[..]);
        let s_status = test_util::expect_reported_status(&s_updates);

        // Send msg #4 to Authenticator.
        let mut a_updates = vec![];
        let result =
            authenticator.on_eapol_frame(&mut a_updates, eapol::Frame::Key(msg4.keyframe()));
        assert!(result.is_ok(), "Authenticator failed processing msg #4: {}", result.unwrap_err());
        assert_eq!(a_updates.len(), 3);
        let a_ptk = test_util::expect_reported_ptk(&a_updates[..]);
        let a_gtk = test_util::expect_reported_gtk(&a_updates[..]);
        let a_status = test_util::expect_reported_status(&a_updates);

        // Verify derived keys match and status reports ESS-SA as established.
        assert_eq!(a_ptk, s_ptk);
        assert_eq!(a_gtk, s_gtk);
        assert_eq!(a_status, SecAssocStatus::EssSaEstablished);
        assert_eq!(s_status, SecAssocStatus::EssSaEstablished);
    }

    #[test]
    fn test_replay_first_message() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message of handshake.
        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        let first_msg2 = expect_eapol_resp(&updates[..]);
        let first_fields = first_msg2.keyframe().key_frame_fields;

        // Replay first message which should restart the entire handshake.
        // Verify the second message of the handshake was received.
        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(3);
        });
        assert!(result.is_ok());
        let second_msg2 = expect_eapol_resp(&updates[..]);
        let second_fields = second_msg2.keyframe().key_frame_fields;

        // Verify Supplicant responded to the replayed first message and didn't change SNonce.
        assert_eq!(second_fields.key_replay_counter.to_native(), 3);
        assert_eq!(first_fields.key_nonce, second_fields.key_nonce);
    }

    #[test]
    fn test_zero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |_| {});
        assert!(result.is_ok());
    }

    #[test]
    fn test_nonzero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, _) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_lower_msg3_counter() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_valid_msg3() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_zero_key_replay_counter_replayed_msg3() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(2);
        });
        assert!(result.is_ok());

        // The just sent third message increased the key replay counter.
        // All successive EAPOL frames are required to have a larger key replay counter.

        // Send an invalid message.
        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(2);
        });
        assert!(result.is_err());

        // Send a valid message.
        let (result, _) = send_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(3);
        });
        assert!(result.is_ok());
    }

    // Replays the first message of the 4-Way Handshake with an altered ANonce to verify that
    // (1) the Supplicant discards the first derived PTK in favor of a new one, and
    // (2) the Supplicant is not reusing a nonce from its previous message,
    // (3) the Supplicant only reports a new PTK if the 4-Way Handshake was completed successfully.
    #[test]
    fn test_replayed_msg1_ptk_installation_different_anonces() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send 1st message of 4-Way Handshake for the first time and derive PTK.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        let msg2 = expect_eapol_resp(&updates[..]);
        let msg2_frame = msg2.keyframe();
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let first_ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let first_nonce = msg2_frame.key_frame_fields.key_nonce;

        // Send 1st message of 4-Way Handshake a second time and derive PTK.
        // Use a different ANonce than initially used.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(2);
            msg1.key_frame_fields.key_nonce = [99; 32];
        });
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        let msg2 = expect_eapol_resp(&updates[..]);
        let msg2_frame = msg2.keyframe();
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let second_ptk = test_util::get_ptk(&[99; 32][..], &snonce[..]);
        let second_nonce = msg2_frame.key_frame_fields.key_nonce;

        // Send 3rd message of 4-Way Handshake.
        // The Supplicant now finished the 4-Way Handshake and should report its PTK.
        // Use the same ANonce which was used in the replayed 1st message.
        let (_, updates) = send_msg3(&mut supplicant, &second_ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(3);
            msg3.key_frame_fields.key_nonce = [99; 32];
        });

        let installed_ptk = test_util::expect_reported_ptk(&updates[..]);
        assert_ne!(first_nonce, second_nonce);
        assert_ne!(&first_ptk, &second_ptk);
        assert_eq!(installed_ptk, second_ptk);
    }

    // Replays the first message of the 4-Way Handshake without altering its ANonce to verify that
    // (1) the Supplicant derives the same PTK for the replayed message, and
    // (2) the Supplicant is reusing the nonce from its previous message,
    // (3) the Supplicant only reports a PTK if the 4-Way Handshake was completed successfully.
    // Regression test for: WLAN-1095
    #[test]
    fn test_replayed_msg1_ptk_installation_same_anonces() {
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send 1st message of 4-Way Handshake for the first time and derive PTK.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        let msg2 = expect_eapol_resp(&updates[..]);
        let msg2_frame = msg2.keyframe();
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let first_ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let first_nonce = msg2_frame.key_frame_fields.key_nonce;

        // Send 1st message of 4-Way Handshake a second time and derive PTK.
        let (_, updates) = send_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(2);
        });
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        let msg2 = expect_eapol_resp(&updates[..]);
        let msg2_frame = msg2.keyframe();
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let second_ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let second_nonce = msg2_frame.key_frame_fields.key_nonce;

        // Send 3rd message of 4-Way Handshake.
        // The Supplicant now finished the 4-Way Handshake and should report its PTK.
        let (_, updates) = send_msg3(&mut supplicant, &second_ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(3);
        });

        let installed_ptk = test_util::expect_reported_ptk(&updates[..]);
        assert_eq!(first_nonce, second_nonce);
        assert_eq!(&first_ptk, &second_ptk);
        assert_eq!(installed_ptk, second_ptk);
    }

    // Test for WPA2-Personal (PSK CCMP-128) with a Supplicant role.
    #[test]
    fn test_supplicant_wpa2_ccmp128_psk() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message
        let (result, updates) = send_msg1(&mut supplicant, |_| {});
        assert!(result.is_ok());

        // Verify 2nd message.
        let msg2_buf = expect_eapol_resp(&updates[..]);
        let msg2 = msg2_buf.keyframe();
        let s_rsne = test_util::get_s_rsne();
        let s_rsne_data = test_util::get_rsne_bytes(&s_rsne);
        assert_eq!({ msg2.eapol_fields.version }, eapol::ProtocolVersion::IEEE802DOT1X2001);
        assert_eq!({ msg2.eapol_fields.packet_type }, eapol::PacketType::KEY);
        let mut buf = vec![];
        msg2.write_into(false, &mut buf).expect("error converting message to bytes");
        assert_eq!(msg2.eapol_fields.packet_body_len.to_native() as usize, buf.len() - 4);
        assert_eq!({ msg2.key_frame_fields.descriptor_type }, eapol::KeyDescriptor::IEEE802DOT11);
        assert_eq!(msg2.key_frame_fields.key_info(), eapol::KeyInformation(0x010A));
        assert_eq!(msg2.key_frame_fields.key_len.to_native(), 0);
        assert_eq!(msg2.key_frame_fields.key_replay_counter.to_native(), 1);
        assert!(!test_util::is_zero(&msg2.key_frame_fields.key_nonce[..]));
        assert!(test_util::is_zero(&msg2.key_frame_fields.key_iv[..]));
        assert_eq!(msg2.key_frame_fields.key_rsc.to_native(), 0);
        assert!(!test_util::is_zero(&msg2.key_mic[..]));
        assert_eq!(msg2.key_mic.len(), test_util::mic_len());
        assert_eq!(msg2.key_data.len(), 20);
        assert_eq!(&msg2.key_data[..], &s_rsne_data[..]);

        // Send 3rd message.
        let snonce = msg2.key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let (result, updates) = send_msg3(&mut supplicant, &ptk, |_| {});
        assert!(result.is_ok());

        // Verify 4th message was received and is correct.
        let msg4_buf = expect_eapol_resp(&updates[..]);
        let msg4 = msg4_buf.keyframe();
        assert_eq!({ msg4.eapol_fields.version }, eapol::ProtocolVersion::IEEE802DOT1X2001);
        assert_eq!({ msg4.eapol_fields.packet_type }, eapol::PacketType::KEY);
        assert_eq!(msg4.eapol_fields.packet_body_len.to_native() as usize, &msg4_buf[..].len() - 4);
        assert_eq!({ msg4.key_frame_fields.descriptor_type }, eapol::KeyDescriptor::IEEE802DOT11);
        assert_eq!(msg4.key_frame_fields.key_info(), eapol::KeyInformation(0x030A));
        assert_eq!(msg4.key_frame_fields.key_len.to_native(), 0);
        assert_eq!(msg4.key_frame_fields.key_replay_counter.to_native(), 2);
        assert!(test_util::is_zero(&msg4.key_frame_fields.key_nonce[..]));
        assert!(test_util::is_zero(&msg4.key_frame_fields.key_iv[..]));
        assert_eq!(msg4.key_frame_fields.key_rsc.to_native(), 0);
        assert!(!test_util::is_zero(&msg4.key_mic[..]));
        assert_eq!(msg4.key_mic.len(), test_util::mic_len());
        assert_eq!(msg4.key_data.len(), 0);
        assert!(test_util::is_zero(&msg4.key_data[..]));
        // Verify the message's MIC.
        let mic = compute_mic(ptk.kck(), &test_util::get_rsne_protection(), &msg4)
            .expect("error computing MIC");
        assert_eq!(&msg4.key_mic[..], &mic[..]);

        // Verify PTK was reported.
        let reported_ptk = test_util::expect_reported_ptk(&updates[..]);
        assert_eq!(ptk.ptk, reported_ptk.ptk);

        // Verify GTK was reported.
        let reported_gtk = test_util::expect_reported_gtk(&updates[..]);
        assert_eq!(&GTK[..], &reported_gtk.gtk[..]);

        // Verify ESS was reported to be established.
        let reported_status = test_util::expect_reported_status(&updates[..]);
        assert_eq!(reported_status, SecAssocStatus::EssSaEstablished);

        // Cause re-keying of GTK via Group-Key Handshake.

        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, GTK_REKEY, 3, 3);
        assert!(result.is_ok());

        // Verify 2th message was received and is correct.
        let msg2_buf = expect_eapol_resp(&updates[..]);
        let msg2 = msg2_buf.keyframe();
        assert_eq!({ msg2.eapol_fields.version }, eapol::ProtocolVersion::IEEE802DOT1X2001);
        assert_eq!({ msg2.eapol_fields.packet_type }, eapol::PacketType::KEY);
        assert_eq!(msg2.eapol_fields.packet_body_len.to_native() as usize, &msg2_buf[..].len() - 4);
        assert_eq!({ msg2.key_frame_fields.descriptor_type }, eapol::KeyDescriptor::IEEE802DOT11);
        assert_eq!(msg2.key_frame_fields.key_info(), eapol::KeyInformation(0x0302));
        assert_eq!(msg2.key_frame_fields.key_len.to_native(), 0);
        assert_eq!(msg2.key_frame_fields.key_replay_counter.to_native(), 3);
        assert!(test_util::is_zero(&msg2.key_frame_fields.key_nonce[..]));
        assert!(test_util::is_zero(&msg2.key_frame_fields.key_iv[..]));
        assert_eq!(msg2.key_frame_fields.key_rsc.to_native(), 0);
        assert!(!test_util::is_zero(&msg2.key_mic[..]));
        assert_eq!(msg2.key_mic.len(), test_util::mic_len());
        assert_eq!(msg2.key_data.len(), 0);
        assert!(test_util::is_zero(&msg2.key_data[..]));
        // Verify the message's MIC.
        let mic = compute_mic(ptk.kck(), &test_util::get_rsne_protection(), &msg2)
            .expect("error computing MIC");
        assert_eq!(&msg2.key_mic[..], &mic[..]);

        // Verify PTK was NOT re-installed.
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);

        // Verify GTK was installed.
        let reported_gtk = test_util::expect_reported_gtk(&updates[..]);
        assert_eq!(&GTK_REKEY[..], &reported_gtk.gtk[..]);
    }

    // Test to verify that GTKs derived in the 4-Way Handshake are not being re-installed
    // through Group Key Handshakes.
    #[test]
    fn test_supplicant_no_gtk_reinstallation_from_4way() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Complete 4-Way Handshake.
        let updates = send_msg1(&mut supplicant, |_| {}).1;
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let _ = send_msg3(&mut supplicant, &ptk, |_| {});

        // Cause re-keying of GTK via Group-Key Handshake.
        // Rekey same GTK which has been already installed via the 4-Way Handshake.
        // This GTK should not be re-installed.
        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, GTK, 2, 3);
        assert!(result.is_ok());

        // Verify 2th message was received and is correct.
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let keyframe = msg2.keyframe();
        assert_eq!(keyframe.eapol_fields.version, eapol::ProtocolVersion::IEEE802DOT1X2001);
        assert_eq!(keyframe.eapol_fields.packet_type, eapol::PacketType::KEY);
        assert_eq!(keyframe.eapol_fields.packet_body_len.to_native() as usize, msg2.len() - 4);
        assert_eq!(keyframe.key_frame_fields.descriptor_type, eapol::KeyDescriptor::IEEE802DOT11);
        assert_eq!(keyframe.key_frame_fields.key_info().0, 0x0302);
        assert_eq!(keyframe.key_frame_fields.key_len.to_native(), 0);
        assert_eq!(keyframe.key_frame_fields.key_replay_counter.to_native(), 3);
        assert!(test_util::is_zero(&keyframe.key_frame_fields.key_nonce[..]));
        assert!(test_util::is_zero(&keyframe.key_frame_fields.key_iv[..]));
        assert_eq!(keyframe.key_frame_fields.key_rsc.to_native(), 0);
        assert!(!test_util::is_zero(&keyframe.key_mic[..]));
        assert_eq!(keyframe.key_mic.len(), test_util::mic_len());
        assert_eq!(keyframe.key_data.len(), 0);
        assert!(test_util::is_zero(&keyframe.key_data[..]));
        // Verify the message's MIC.
        let mic = compute_mic(ptk.kck(), &test_util::get_rsne_protection(), &keyframe)
            .expect("error computing MIC");
        assert_eq!(&keyframe.key_mic[..], &mic[..]);

        // Verify neither PTK nor GTK were re-installed.
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        assert_eq!(test_util::get_reported_gtk(&updates[..]), None);
    }

    // Test to verify that already rotated GTKs are not being re-installed.
    #[test]
    fn test_supplicant_no_gtk_reinstallation() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Complete 4-Way Handshake.
        let updates = send_msg1(&mut supplicant, |_| {}).1;
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let _ = send_msg3(&mut supplicant, &ptk, |_| {});

        // Cause re-keying of GTK via Group-Key Handshake.
        // Rekey same GTK which has been already installed via the 4-Way Handshake.
        // This GTK should not be re-installed.

        // Rotate GTK.
        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, GTK_REKEY, 3, 3);
        assert!(result.is_ok());
        let reported_gtk = test_util::expect_reported_gtk(&updates[..]);
        assert_eq!(&reported_gtk.gtk[..], &GTK_REKEY[..]);

        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, GTK_REKEY_2, 1, 4);
        assert!(result.is_ok(), "{:?}", result);
        let reported_gtk = test_util::expect_reported_gtk(&updates[..]);
        assert_eq!(&reported_gtk.gtk[..], &GTK_REKEY_2[..]);

        // Rotate GTK to already installed key. Verify GTK was not re-installed.
        let (result, updates) = send_group_key_msg1(&mut supplicant, &ptk, GTK_REKEY, 3, 5);
        assert!(result.is_ok());
        assert_eq!(test_util::get_reported_gtk(&updates[..]), None);
    }

    // TODO(hahnr): Add additional tests:
    // Invalid messages from Authenticator
    // Timeouts
    // Nonce reuse
    // (in)-compatible protocol and RSNE versions

    fn send_msg1<F>(
        supplicant: &mut Supplicant,
        msg_modifier: F,
    ) -> (Result<(), anyhow::Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrameTx),
    {
        let msg = test_util::get_4whs_msg1(&ANONCE[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        (result, update_sink)
    }

    fn send_msg3<F>(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        msg_modifier: F,
    ) -> (Result<(), anyhow::Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrameTx),
    {
        let msg = test_util::get_4whs_msg3(ptk, &ANONCE[..], &GTK[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        (result, update_sink)
    }

    fn send_group_key_msg1(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        gtk: [u8; 16],
        key_id: u8,
        key_replay_counter: u64,
    ) -> (Result<(), anyhow::Error>, UpdateSink) {
        let msg = test_util::get_group_key_hs_msg1(ptk, &gtk[..], key_id, key_replay_counter);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        (result, update_sink)
    }
}
