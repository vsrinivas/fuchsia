// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::key::exchange::{
    self,
    handshake::{fourway::Fourway, group_key::GroupKey},
    Key,
};
use crate::key::{gtk::Gtk, igtk::Igtk, ptk::Ptk};
use crate::rsna::{
    Dot11VerifiedKeyFrame, NegotiatedProtection, Role, SecAssocStatus, SecAssocUpdate, UpdateSink,
};
use crate::{format_rsn_err, Error};
use eapol;
use fidl_fuchsia_wlan_mlme::EapolResultCode;
use log::{error, info};
use std::collections::HashSet;
use wlan_statemachine::StateMachine;
use zerocopy::ByteSlice;

const MAX_KEY_FRAME_RETRIES: u32 = 3;

#[derive(Debug)]
enum Pmksa {
    Initialized { pmk: Option<Vec<u8>> },
    Established { pmk: Vec<u8> },
}

impl Pmksa {
    fn reset(self) -> Self {
        match self {
            Pmksa::Established { pmk } | Pmksa::Initialized { pmk: Some(pmk) } => {
                Pmksa::Initialized { pmk: Some(pmk) }
            }
            _ => Pmksa::Initialized { pmk: None },
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

/// Igtksa is super simple because there's currently no method for populating it other than an adjacent Gtksa.
#[derive(Debug)]
enum Igtksa {
    Uninitialized,
    Established { installed_igtks: HashSet<Igtk> },
}

impl Igtksa {
    fn reset(self) -> Self {
        Igtksa::Uninitialized
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
#[derive(Debug)]
pub(crate) struct EssSa {
    // Determines the device's role (Supplicant or Authenticator).
    role: Role,
    // The protection used for this association.
    pub negotiated_protection: NegotiatedProtection,
    // The last valid key replay counter. Messages with a key replay counter lower than this counter
    // value will be dropped.
    key_replay_counter: u64,
    // A retry counter and key frame to resend if a timeout is received while waiting for a response.
    last_key_frame_buf: Option<(u32, eapol::KeyFrameBuf)>,
    // Updates to send after we receive an eapol send confirmation. This will contain an empty update
    // sink if we're awaiting a confirm but do not have any subsequent updates to send.
    updates_awaiting_confirm: Option<UpdateSink>,

    // Individual Security Associations.
    pmksa: StateMachine<Pmksa>,
    ptksa: StateMachine<Ptksa>,
    gtksa: StateMachine<Gtksa>,
    igtksa: StateMachine<Igtksa>,
}

// IEEE Std 802.11-2016, 12.6.1.3.2
impl EssSa {
    pub fn new(
        role: Role,
        pmk: Option<Vec<u8>>,
        negotiated_protection: NegotiatedProtection,
        ptk_exch_cfg: exchange::Config,
        gtk_exch_cfg: Option<exchange::Config>,
    ) -> Result<EssSa, anyhow::Error> {
        info!("spawned ESSSA for: {:?}", role);

        let rsna = EssSa {
            role,
            negotiated_protection,
            key_replay_counter: 0,
            last_key_frame_buf: None,
            updates_awaiting_confirm: None,
            pmksa: StateMachine::new(Pmksa::Initialized { pmk }),
            ptksa: StateMachine::new(Ptksa::Uninitialized { cfg: ptk_exch_cfg }),
            gtksa: StateMachine::new(Gtksa::Uninitialized { cfg: gtk_exch_cfg }),
            igtksa: StateMachine::new(Igtksa::Uninitialized),
        };
        Ok(rsna)
    }

    pub fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        // This function will not succeed unless called on a new Esssa or one that was reset.
        match (self.ptksa.as_ref(), self.gtksa.as_ref(), self.igtksa.as_ref()) {
            (Ptksa::Uninitialized { .. }, Gtksa::Uninitialized { .. }, Igtksa::Uninitialized) => (),
            // TODO (fxbug.dev/69388): The Ptksa can be in the Initialized state
            // if the auth method was SAE and the Pmksa key was
            // confirmed prior to initiate()'ing this Esssa.
            (Ptksa::Initialized { .. }, Gtksa::Uninitialized { .. }, Igtksa::Uninitialized) => (),
            _ => return Err(Error::UnexpectedEsssaInitiation),
        };
        info!("establishing ESSSA...");

        // Immediately establish the PMKSA if the key is available. The PMK may be provided
        // during ESSSA construction, or generated by a subsequent auth handshake such as SAE.
        let pmk = match self.pmksa.as_ref() {
            Pmksa::Initialized { pmk: Some(pmk) } => Some(pmk.clone()),
            _ => None,
        };
        if let Some(pmk) = pmk {
            let mut new_updates = UpdateSink::default();
            let result = self.on_key_confirmed(&mut new_updates, Key::Pmk(pmk.clone()));
            self.push_updates(update_sink, new_updates);
            result
        } else {
            Ok(())
        }
    }

    pub fn reset_replay_counter(&mut self) {
        info!("resetting ESSSA replay counter");
        self.key_replay_counter = 0;
    }

    pub fn reset_security_associations(&mut self) {
        info!("resetting ESSSA security associations");
        self.pmksa.replace_state(|state| state.reset());
        self.ptksa.replace_state(|state| state.reset());
        self.gtksa.replace_state(|state| state.reset());
        self.igtksa.replace_state(|state| state.reset());
    }

    fn is_established(&self) -> bool {
        match (self.ptksa.as_ref(), self.gtksa.as_ref()) {
            (Ptksa::Established { .. }, Gtksa::Established { .. }) => true,
            _ => false,
        }
    }

    fn on_key_confirmed(&mut self, update_sink: &mut UpdateSink, key: Key) -> Result<(), Error> {
        let was_esssa_established = self.is_established();
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.replace_state(|state| match state {
                    Pmksa::Initialized { .. } => {
                        info!("established PMKSA");
                        update_sink.push(SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished));
                        Pmksa::Established { pmk: pmk.clone() }
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
                    return Err(format_rsn_err!("PTKSA not initialized"));
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
            Key::Igtk(igtk) => {
                self.igtksa.replace_state(|state| match state {
                    Igtksa::Uninitialized => {
                        info!("established IGTKSA");
                        let mut installed_igtks = HashSet::default();
                        installed_igtks.insert(igtk.clone());
                        update_sink.push(SecAssocUpdate::Key(Key::Igtk(igtk)));
                        Igtksa::Established { installed_igtks }
                    }
                    Igtksa::Established { mut installed_igtks } => {
                        info!("re-established new IGTKSA; invalidating previous one");

                        if !installed_igtks.contains(&igtk) {
                            installed_igtks.insert(igtk.clone());
                            update_sink.push(SecAssocUpdate::Key(Key::Igtk(igtk)));
                        }
                        Igtksa::Established { installed_igtks }
                    }
                });
            }
            _ => {}
        };
        Ok(())
    }

    pub fn on_pmk_available(
        &mut self,
        update_sink: &mut UpdateSink,
        pmk: Vec<u8>,
    ) -> Result<(), Error> {
        let mut new_updates = UpdateSink::default();
        let result = self.on_key_confirmed(&mut new_updates, Key::Pmk(pmk));
        self.push_updates(update_sink, new_updates);
        result
    }

    // Do any necessary final processing before passing updates to the higher layer.
    fn push_updates(&mut self, update_sink: &mut UpdateSink, new_updates: UpdateSink) {
        for update in new_updates {
            // If we've sent an eapol frame, buffer all remaining updates until we receive a confirm.
            if let Some(buffered_updates) = &mut self.updates_awaiting_confirm {
                buffered_updates.push(update);
            } else {
                if let SecAssocUpdate::TxEapolKeyFrame { frame, expect_response } = &update {
                    if *expect_response {
                        self.last_key_frame_buf = Some((1, frame.clone()));
                    } else {
                        // We don't expect a response, so we don't need to keep the frame around.
                        self.last_key_frame_buf = None;
                    }
                    self.updates_awaiting_confirm.replace(Default::default());
                }
                update_sink.push(update);
            }
        }
    }

    pub fn on_eapol_conf(
        &mut self,
        update_sink: &mut UpdateSink,
        result: EapolResultCode,
    ) -> Result<(), Error> {
        match self.updates_awaiting_confirm.take() {
            Some(updates) => match result {
                EapolResultCode::Success => {
                    // We successfully sent a frame. Now send the resulting ESSSA updates.
                    self.push_updates(update_sink, updates);
                    Ok(())
                }
                EapolResultCode::TransmissionFailure => Err(Error::KeyFrameTransmissionFailed),
            },
            None => {
                error!("Ignored unexpected eapol send confirm");
                Ok(())
            }
        }
    }

    pub fn on_key_frame_timeout(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        // IEEE Std 802.11-2016 6.3.22.2.4: We should always receive a confirm in response to an eapol tx.
        // If we never received an eapol conf, treat this as a fatal error.
        if let Some(updates) = &self.updates_awaiting_confirm {
            return Err(Error::NoKeyFrameTransmissionConfirm(updates.len()));
        }
        // Resend the last key frame if appropriate
        if let Some((attempt, key_frame)) = self.last_key_frame_buf.as_mut() {
            *attempt += 1;
            if *attempt > MAX_KEY_FRAME_RETRIES {
                return Err(Error::TooManyKeyFrameRetries);
            }
            update_sink.push(SecAssocUpdate::TxEapolKeyFrame {
                frame: key_frame.clone(),
                expect_response: true,
            });
        }
        Ok(())
    }

    pub fn on_eapol_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::Frame<B>,
    ) -> Result<(), Error> {
        let mut new_updates = UpdateSink::default();
        // Only processes EAPOL Key frames. Drop all other frames silently.
        let updates = match frame {
            eapol::Frame::Key(key_frame) => {
                let mut updates = UpdateSink::default();
                self.on_eapol_key_frame(&mut updates, key_frame)?;
                // We've received a new key frame, so don't retransmit our last one.
                self.last_key_frame_buf.take();

                // Authenticator updates its key replay counter with every outbound EAPOL frame.
                if let Role::Authenticator = self.role {
                    for update in &updates {
                        if let SecAssocUpdate::TxEapolKeyFrame { frame, .. } = update {
                            let krc =
                                frame.keyframe().key_frame_fields.key_replay_counter.to_native();

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
                    if let Err(e) = self.on_key_confirmed(&mut new_updates, key) {
                        error!("error while processing key: {}", e);
                    };
                }
                // Forward all other updates.
                _ => new_updates.push(update),
            }
        }

        // Report once ESSSA is established.
        if !was_esssa_established && self.is_established() {
            info!("established ESSSA");
            new_updates.push(SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));
        }

        self.push_updates(update_sink, new_updates);

        Ok(())
    }

    fn on_eapol_key_frame<B: ByteSlice>(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: eapol::KeyFrameRx<B>,
    ) -> Result<(), Error> {
        // Verify the frame complies with IEEE Std 802.11-2016, 12.7.2.
        let verified_frame = match Dot11VerifiedKeyFrame::from_frame(
            frame,
            &self.role,
            &self.negotiated_protection,
            self.key_replay_counter,
        ) {
            // An invalid key replay counter means we should skip the frame, but may happen under
            // normal circumstances and should not be logged as an error.
            Err(e @ Error::InvalidKeyReplayCounter(_, _)) => {
                info!("Ignoring eapol frame: {}", e);
                return Ok(());
            }
            result => result?,
        };

        // Safe: frame was just verified.
        let raw_frame = verified_frame.unsafe_get_raw();

        // IEEE Std 802.11-2016, 12.7.2, d)
        // Update key replay counter if MIC was set and is valid. Only applicable for Supplicant.
        // TODO(fxbug.dev/68916): We should verify the MIC here and only increase the counter if the MIC
        // is valid.
        if raw_frame.key_frame_fields.key_info().key_mic() {
            if let Role::Supplicant = self.role {
                self.key_replay_counter = raw_frame.key_frame_fields.key_replay_counter.to_native();
            }
        }

        // Forward frame to correct security association.
        // PMKSA must be established before any other security association can be established. Because
        // the PMKSA is handled outside our ESSSA this is just an early return.
        match self.pmksa.as_mut() {
            Pmksa::Initialized { .. } => return Ok(()),
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
    use crate::rsna::AuthStatus;
    use crate::{Authenticator, Supplicant};
    use wlan_common::assert_variant;
    use wlan_common::ie::{get_rsn_ie_bytes, rsn::fake_wpa2_s_rsne};

    const ANONCE: [u8; 32] = [0x1A; 32];
    const GTK: [u8; 16] = [0x1B; 16];
    const GTK_REKEY: [u8; 16] = [0x1F; 16];
    const GTK_REKEY_2: [u8; 16] = [0x2F; 16];

    #[test]
    fn test_supplicant_with_wpa3_authenticator() {
        let mut supplicant = test_util::get_wpa3_supplicant();
        let mut authenticator = test_util::get_wpa3_authenticator();
        supplicant.start().expect("Failed starting Supplicant");

        // Send Supplicant SAE commit
        let mut s_updates = vec![];
        let result = supplicant.on_sae_handshake_ind(&mut s_updates);
        assert!(result.is_ok(), "Supplicant failed to ind SAE handshake");
        let s_sae_frame_vec = test_util::expect_sae_frame_vec(&s_updates[..]);
        test_util::expect_schedule_sae_timeout(&s_updates[..]);
        assert_eq!(s_updates.len(), 2, "{:?}", s_updates);

        // Respond to Supplicant SAE commit
        let mut a_updates = vec![];
        for s_sae_frame in s_sae_frame_vec {
            let result = authenticator.on_sae_frame_rx(&mut a_updates, s_sae_frame);
            assert!(result.is_ok(), "Authenticator failed to rx SAE handshake message");
        }
        let a_sae_frame_vec = test_util::expect_sae_frame_vec(&a_updates[..]);
        test_util::expect_schedule_sae_timeout(&a_updates[..]);
        assert_eq!(a_updates.len(), 3, "{:?}", a_updates);

        // Receive Authenticator SAE confirm
        let mut s_updates = vec![];
        for a_sae_frame in a_sae_frame_vec {
            let result = supplicant.on_sae_frame_rx(&mut s_updates, a_sae_frame);
            assert!(result.is_ok(), "Supplicant failed to rx SAE handshake message");
        }
        let s_sae_frame_vec = test_util::expect_sae_frame_vec(&s_updates[..]);
        test_util::expect_schedule_sae_timeout(&s_updates[..]);
        test_util::expect_reported_pmk(&s_updates[..]);
        test_util::expect_reported_sae_auth_status(&s_updates[..], AuthStatus::Success);
        test_util::expect_reported_status(&s_updates[..], SecAssocStatus::PmkSaEstablished);
        assert_eq!(s_updates.len(), 5, "{:?}", s_updates);

        // Receive Supplicant SAE confirm
        let mut a_updates = vec![];
        for s_sae_frame in s_sae_frame_vec {
            let result = authenticator.on_sae_frame_rx(&mut a_updates, s_sae_frame);
            assert!(result.is_ok(), "Authenticator failed to rx SAE handshake message");
        }
        test_util::expect_reported_pmk(&a_updates[..]);
        test_util::expect_reported_sae_auth_status(&a_updates[..], AuthStatus::Success);
        test_util::expect_reported_status(&a_updates[..], SecAssocStatus::PmkSaEstablished);
        let msg1 = test_util::expect_eapol_resp(&a_updates[..]);
        authenticator
            .on_eapol_conf(&mut a_updates, EapolResultCode::Success)
            .expect("Failed eapol conf");
        assert_eq!(a_updates.len(), 4, "{:?}", a_updates);

        test_eapol_exchange(&mut supplicant, &mut authenticator, Some(msg1), true);
    }

    #[test]
    fn test_supplicant_with_wpa2_authenticator() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        let mut authenticator = test_util::get_wpa2_authenticator();
        supplicant.start().expect("Failed starting Supplicant");
        test_eapol_exchange(&mut supplicant, &mut authenticator, None, false);
    }

    #[test]
    fn test_replay_first_message() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message of handshake.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        let first_msg2 = expect_eapol_resp(&updates[..]);
        let first_fields = first_msg2.keyframe().key_frame_fields;

        // Replay first message which should restart the entire handshake.
        // Verify the second message of the handshake was received.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
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
    fn test_first_message_does_not_change_replay_counter() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        // Send first message of handshake.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        expect_eapol_resp(&updates[..]);
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        // Raise the replay counter of message 1.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        expect_eapol_resp(&updates[..]);
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        // Lower the replay counter of message 1.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        assert_eq!(0, supplicant.esssa.key_replay_counter);
        expect_eapol_resp(&updates[..]);
    }

    #[test]
    fn test_zero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        expect_eapol_resp(&updates[..]);
    }

    #[test]
    fn test_nonzero_key_replay_counter_msg1() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        expect_eapol_resp(&updates[..]);
    }

    #[test]
    fn test_zero_key_replay_counter_lower_msg3_counter() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        // Intuitively, this should not succeed because the replay
        // counter in message 3 is lower than in message 1. It is a
        // quirk of IEEE 802.11-2016 12.7.2 that the replay counter in
        // message 1 is in fact meaningless because replay counters
        // are only updated when there is a MIC to verify. There is no
        // MIC to verify in message 1, and so the replay counter
        // doesn't matter.
        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        assert_eq!(0, supplicant.esssa.key_replay_counter);
        test_util::expect_reported_ptk(&updates[..]);
    }

    #[test]
    fn test_key_replay_counter_updated_after_msg3() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(5);
        });
        assert!(result.is_ok());
        assert_eq!(5, supplicant.esssa.key_replay_counter);
        test_util::expect_reported_ptk(&updates[..]);

        // First message should be dropped if replay counter too low.
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        assert_eq!(5, supplicant.esssa.key_replay_counter);
        assert!(updates.is_empty());

        // After reset, first message should not be dropped.
        supplicant.reset();
        supplicant.start().expect("Failed starting Supplicant");
        assert_eq!(0, supplicant.esssa.key_replay_counter);
        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        expect_eapol_resp(&updates[..]);
    }

    #[test]
    fn test_zero_key_replay_counter_valid_msg3() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert!(result.is_ok());
        test_util::expect_reported_ptk(&updates[..]);
    }

    #[test]
    fn test_zero_key_replay_counter_replayed_msg3() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");
        assert_eq!(0, supplicant.esssa.key_replay_counter);

        let (result, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(0);
        });
        assert!(result.is_ok());
        let msg2 = expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);

        let (result, sink) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(2);
        });
        assert!(result.is_ok());
        assert_eq!(2, supplicant.esssa.key_replay_counter);
        test_util::expect_reported_ptk(&sink[..]);

        // The just sent third message increased the key replay counter.
        // All successive EAPOL frames are required to have a larger key replay counter.

        // Send an invalid message.
        let (result, sink) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(2);
        });
        assert!(result.is_ok());
        assert_eq!(2, supplicant.esssa.key_replay_counter);
        assert!(sink.is_empty());

        // Send a valid message.
        let (result, sink) = send_fourway_msg3(&mut supplicant, &ptk, |msg3| {
            msg3.key_frame_fields.key_replay_counter.set_from_native(3);
        });
        assert!(result.is_ok());
        assert_eq!(3, supplicant.esssa.key_replay_counter);
        assert!(!sink.is_empty());
    }

    // Replays the first message of the 4-Way Handshake with an altered ANonce to verify that
    // (1) the Supplicant discards the first derived PTK in favor of a new one, and
    // (2) the Supplicant is not reusing a nonce from its previous message,
    // (3) the Supplicant only reports a new PTK if the 4-Way Handshake was completed successfully.
    #[test]
    fn test_replayed_msg1_ptk_installation_different_anonces() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send 1st message of 4-Way Handshake for the first time and derive PTK.
        let (_, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
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
        let (_, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
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
        let (_, updates) = send_fourway_msg3(&mut supplicant, &second_ptk, |msg3| {
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
    // Regression test for: fxbug.dev/29713
    #[test]
    fn test_replayed_msg1_ptk_installation_same_anonces() {
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send 1st message of 4-Way Handshake for the first time and derive PTK.
        let (_, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
            msg1.key_frame_fields.key_replay_counter.set_from_native(1);
        });
        assert_eq!(test_util::get_reported_ptk(&updates[..]), None);
        let msg2 = expect_eapol_resp(&updates[..]);
        let msg2_frame = msg2.keyframe();
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let first_ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let first_nonce = msg2_frame.key_frame_fields.key_nonce;

        // Send 1st message of 4-Way Handshake a second time and derive PTK.
        let (_, updates) = send_fourway_msg1(&mut supplicant, |msg1| {
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
        let (_, updates) = send_fourway_msg3(&mut supplicant, &second_ptk, |msg3| {
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
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send first message
        let (result, updates) = send_fourway_msg1(&mut supplicant, |_| {});
        assert!(result.is_ok());

        // Verify 2nd message.
        let msg2_buf = expect_eapol_resp(&updates[..]);
        let msg2 = msg2_buf.keyframe();
        let s_rsne = fake_wpa2_s_rsne();
        let s_rsne_data = get_rsn_ie_bytes(&s_rsne);
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
        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |_| {});
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
        let reported_status =
            test_util::expect_reported_status(&updates[..], SecAssocStatus::EssSaEstablished);
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
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Complete 4-Way Handshake.
        let updates = send_fourway_msg1(&mut supplicant, |_| {}).1;
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let _ = send_fourway_msg3(&mut supplicant, &ptk, |_| {});

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
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Complete 4-Way Handshake.
        let updates = send_fourway_msg1(&mut supplicant, |_| {}).1;
        let msg2 = test_util::expect_eapol_resp(&updates[..]);
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let _ = send_fourway_msg3(&mut supplicant, &ptk, |_| {});

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

    #[test]
    fn test_key_frame_timeout() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send the first frame.
        let updates = send_fourway_msg1(&mut supplicant, |_| {}).1;
        let msg2 = test_util::expect_eapol_resp(&updates[..]);

        // Timeout several times.
        for _ in 1..MAX_KEY_FRAME_RETRIES {
            let mut update_sink = vec![];
            supplicant
                .on_eapol_key_frame_timeout(&mut update_sink)
                .expect("Failed to send key frame timeout");
            let msg2_retry = test_util::expect_eapol_resp(&update_sink[..]);
            assert_eq!(msg2, msg2_retry);
        }

        // Failure on the last retry.
        let mut update_sink = vec![];
        assert_variant!(
            supplicant.on_eapol_key_frame_timeout(&mut update_sink),
            Err(Error::TooManyKeyFrameRetries)
        );
    }

    #[test]
    fn test_msg2_out_of_order_conf() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send the first frame but don't send a conf in response.
        let msg = test_util::get_wpa2_4whs_msg1(&ANONCE[..], |_| {});
        let mut update_sink = UpdateSink::default();
        supplicant
            .on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()))
            .expect("Failed to send eapol frame");
        let msg2 = test_util::expect_eapol_resp(&update_sink[..]);

        // No key frame confirm means that we will buffer updates until the confirm is received.
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |_| {});
        result.expect("Failed to send msg3");
        assert!(updates.is_empty());

        // When confirm is received we receive updates through msg4
        let mut update_sink = UpdateSink::default();
        supplicant
            .on_eapol_conf(&mut update_sink, EapolResultCode::Success)
            .expect("Failed to send eapol conf");
        assert_eq!(update_sink.len(), 1);
        test_util::expect_eapol_resp(&update_sink[..]);

        // On another confirm, we receive the remaining updates.
        supplicant
            .on_eapol_conf(&mut update_sink, EapolResultCode::Success)
            .expect("Failed to send eapol conf");
        test_util::expect_reported_ptk(&update_sink[..]);
        test_util::expect_reported_gtk(&update_sink[..]);
    }

    #[test]
    fn test_msg2_no_conf() {
        // Create ESS Security Association
        let mut supplicant = test_util::get_wpa2_supplicant();
        supplicant.start().expect("Failed starting Supplicant");

        // Send the first frame but don't send a conf in response.
        let msg = test_util::get_wpa2_4whs_msg1(&ANONCE[..], |_| {});
        let mut update_sink = UpdateSink::default();
        supplicant
            .on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()))
            .expect("Failed to send eapol frame");
        let msg2 = test_util::expect_eapol_resp(&update_sink[..]);

        // No key frame confirm means that we will buffer updates until the confirm is received.
        let snonce = msg2.keyframe().key_frame_fields.key_nonce;
        let ptk = test_util::get_ptk(&ANONCE[..], &snonce[..]);
        let (result, updates) = send_fourway_msg3(&mut supplicant, &ptk, |_| {});
        result.expect("Failed to send msg3");
        assert!(updates.is_empty());

        // We never receive a confirm, and report this on timeout.
        // There are 4 pending updates that we drop after this timeout:
        //   * EAPOL message 4
        //   * PTK
        //   * GTK
        //   * ESSSA Established
        let mut update_sink = vec![];
        assert_variant!(
            supplicant.on_eapol_key_frame_timeout(&mut update_sink),
            Err(Error::NoKeyFrameTransmissionConfirm(4))
        );
    }

    // TODO(hahnr): Add additional tests:
    // Invalid messages from Authenticator
    // Timeouts
    // Nonce reuse
    // (in)-compatible protocol and RSNE versions

    fn test_eapol_exchange(
        supplicant: &mut Supplicant,
        authenticator: &mut Authenticator,
        msg1: Option<eapol::KeyFrameBuf>,
        wpa3: bool,
    ) {
        // Initiate Authenticator.
        let mut a_updates = vec![];
        let result = authenticator.initiate(&mut a_updates);
        assert!(result.is_ok(), "Authenticator failed initiating: {}", result.unwrap_err());

        if wpa3 {
            assert!(
                msg1.is_some(),
                "WPA3 EAPOL exchange starting without message constructed immediately after SAE"
            );
        }
        // If msg1 is provided, we expect no updates from the Authenticator. Otherwise, we
        // expect the Authenticator to establish the PmkSa and produce msg1.
        let msg1 = match msg1 {
            Some(msg1) => {
                assert_eq!(a_updates.len(), 0, "{:?}", a_updates);
                msg1
            }
            None => {
                assert_eq!(a_updates.len(), 2);
                test_util::expect_reported_status(&a_updates, SecAssocStatus::PmkSaEstablished);
                let resp = test_util::expect_eapol_resp(&a_updates[..]);
                authenticator
                    .on_eapol_conf(&mut a_updates, EapolResultCode::Success)
                    .expect("Failed eapol conf");
                resp
            }
        };

        // Send msg #1 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, eapol::Frame::Key(msg1.keyframe()));
        assert!(result.is_ok(), "Supplicant failed processing msg #1: {}", result.unwrap_err());
        let msg2 = test_util::expect_eapol_resp(&s_updates[..]);
        supplicant
            .on_eapol_conf(&mut s_updates, EapolResultCode::Success)
            .expect("Failed eapol conf");
        assert_eq!(s_updates.len(), 1, "{:?}", s_updates);

        // Send msg #2 to Authenticator and wait for response.
        let mut a_updates = vec![];
        let result =
            authenticator.on_eapol_frame(&mut a_updates, eapol::Frame::Key(msg2.keyframe()));
        assert!(result.is_ok(), "Authenticator failed processing msg #2: {}", result.unwrap_err());
        let msg3 = test_util::expect_eapol_resp(&a_updates[..]);
        authenticator
            .on_eapol_conf(&mut a_updates, EapolResultCode::Success)
            .expect("Failed eapol conf");
        assert_eq!(a_updates.len(), 1, "{:?}", a_updates);

        // Send msg #3 to Supplicant and wait for response.
        let mut s_updates = vec![];
        let result = supplicant.on_eapol_frame(&mut s_updates, eapol::Frame::Key(msg3.keyframe()));
        assert!(result.is_ok(), "Supplicant failed processing msg #3: {}", result.unwrap_err());
        assert_eq!(s_updates.len(), 1); // We shouldn't see the ptk or gtk until an eapol conf is received.
        let msg4 = test_util::expect_eapol_resp(&s_updates[..]);
        supplicant
            .on_eapol_conf(&mut s_updates, EapolResultCode::Success)
            .expect("Failed eapol conf");
        let s_ptk = test_util::expect_reported_ptk(&s_updates[..]);
        let s_gtk = test_util::expect_reported_gtk(&s_updates[..]);
        let s_igtk = if wpa3 {
            assert_eq!(s_updates.len(), 5, "{:?}", s_updates);
            Some(test_util::expect_reported_igtk(&s_updates[..]))
        } else {
            assert_eq!(s_updates.len(), 4, "{:?}", s_updates);
            None
        };
        test_util::expect_reported_status(&s_updates, SecAssocStatus::EssSaEstablished);

        // Send msg #4 to Authenticator.
        let mut a_updates = vec![];
        let result =
            authenticator.on_eapol_frame(&mut a_updates, eapol::Frame::Key(msg4.keyframe()));
        assert!(result.is_ok(), "Authenticator failed processing msg #4: {}", result.unwrap_err());
        let a_ptk = test_util::expect_reported_ptk(&a_updates[..]);
        let a_gtk = test_util::expect_reported_gtk(&a_updates[..]);

        let a_igtk = if wpa3 {
            assert_eq!(a_updates.len(), 4, "{:?}", a_updates);
            Some(test_util::expect_reported_igtk(&a_updates[..]))
        } else {
            assert_eq!(a_updates.len(), 3, "{:?}", a_updates);
            None
        };

        test_util::expect_reported_status(&a_updates, SecAssocStatus::EssSaEstablished);

        // Verify derived keys match and status reports ESS-SA as established.
        assert_eq!(a_ptk, s_ptk);
        assert_eq!(a_gtk, s_gtk);
        assert_eq!(a_igtk, s_igtk);
    }

    fn send_eapol_conf(
        supplicant: &mut Supplicant,
        update_sink: &mut UpdateSink,
    ) -> Result<(), Error> {
        let mut sent_frame = false;
        for update in &update_sink[..] {
            if let SecAssocUpdate::TxEapolKeyFrame { .. } = update {
                sent_frame = true;
            }
        }
        if sent_frame {
            supplicant.on_eapol_conf(update_sink, EapolResultCode::Success)
        } else {
            Ok(())
        }
    }

    fn send_fourway_msg1<F>(
        supplicant: &mut Supplicant,
        msg_modifier: F,
    ) -> (Result<(), Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrameTx),
    {
        let msg = test_util::get_wpa2_4whs_msg1(&ANONCE[..], msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        let result = result.and_then(|_| send_eapol_conf(supplicant, &mut update_sink));
        (result, update_sink)
    }

    fn send_fourway_msg3<F>(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        msg_modifier: F,
    ) -> (Result<(), Error>, UpdateSink)
    where
        F: Fn(&mut eapol::KeyFrameTx),
    {
        let msg = test_util::get_wpa2_4whs_msg3(ptk, &ANONCE[..], &GTK, msg_modifier);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        let result = result.and_then(|_| send_eapol_conf(supplicant, &mut update_sink));
        (result, update_sink)
    }

    fn send_group_key_msg1(
        supplicant: &mut Supplicant,
        ptk: &Ptk,
        gtk: [u8; 16],
        key_id: u8,
        key_replay_counter: u64,
    ) -> (Result<(), Error>, UpdateSink) {
        let msg = test_util::get_group_key_hs_msg1(ptk, &gtk[..], key_id, key_replay_counter);
        let mut update_sink = UpdateSink::default();
        let result = supplicant.on_eapol_frame(&mut update_sink, eapol::Frame::Key(msg.keyframe()));
        let result = result.and_then(|_| send_eapol_conf(supplicant, &mut update_sink));
        (result, update_sink)
    }
}
