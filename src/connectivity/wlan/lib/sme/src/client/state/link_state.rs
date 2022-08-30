// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{now, Protection, StateChangeContext, StateChangeContextExt},
    crate::{
        client::{
            event::{self, Event, RsnaCompletionTimeout, RsnaResponseTimeout},
            internal::Context,
            rsn::Rsna,
            EstablishRsnaFailureReason,
        },
        MlmeRequest, MlmeSink,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_inspect_contrib::{inspect_log, log::InspectBytes},
    fuchsia_zircon as zx,
    ieee80211::{Bssid, MacAddr, WILDCARD_BSSID},
    log::{error, warn},
    wlan_common::{bss::BssDescription, timer::EventId},
    wlan_rsn::{
        key::{exchange::Key, Tk},
        rsna::{self, SecAssocStatus, SecAssocUpdate},
    },
    wlan_statemachine::*,
};

#[derive(Debug)]
pub struct Init;

#[derive(Debug)]
pub struct EstablishingRsna {
    pub rsna: Rsna,
    // Timeout for the total duration RSNA may take to complete. This timeout is
    // never rescheduled.
    pub rsna_completion_timeout: Option<EventId>,
    // Timeout for the duration RSNA will await a response after transmitting a frame.
    // This timeout will be rescheduled upon receiving each valid frame.
    pub rsna_response_timeout: Option<EventId>,
    // Timeout for the duration RSNA will await a response after transmitting a frame
    // before possibly retransmitting the same frame. This timeout will be rescheduled
    // upon transmitting each frame.
    pub rsna_retransmission_timeout: Option<EventId>,

    // The following conditions must all be satisfied to consider an RSNA established. They
    // may be satisfied in multiple orders, so we represent them as a threshold rather than
    // as additional link states.
    // Indicates that all handshake frames have been sent or received.
    pub handshake_complete: bool,
    // If empty, indicates that we have received confirms for all keys we've installed.
    pub pending_key_ids: std::collections::HashSet<u16>,
}

#[derive(Debug)]
pub struct LinkUp {
    pub protection: Protection,
    pub since: zx::Time,
}

statemachine!(
    #[derive(Debug)]
    pub enum LinkState,
    () => Init,
    // If the association does not use an Rsna, we move directly from Init to LinkUp.
    Init => [EstablishingRsna, LinkUp],
    EstablishingRsna => LinkUp,
);

#[derive(Debug)]
enum RsnaStatus {
    Failed(EstablishRsnaFailureReason),
    Unchanged,
    Progressed {
        ap_responsive: Option<EventId>,
        new_retransmission_timeout: Option<EventId>,
        handshake_complete: bool,
        sent_keys: Vec<u16>,
    },
}

#[derive(Debug)]
enum RsnaProgressed {
    Complete(LinkUp),
    InProgress(EstablishingRsna),
}

impl EstablishingRsna {
    fn on_rsna_progressed(
        mut self,
        ap_responsive: Option<EventId>,
        rsna_retransmission_timeout: Option<EventId>,
        handshake_complete: bool,
        sent_keys: Vec<u16>,
    ) -> Self {
        sent_keys.into_iter().for_each(|key| {
            self.pending_key_ids.insert(key);
        });
        self.handshake_complete |= handshake_complete;

        // Always cancel the retransmission timeout if RSNA progresssed since,
        // once transmitting frames, all meaningful progression implies the last
        // tranmsmitted frame resulted in progress.
        cancel(&mut self.rsna_retransmission_timeout);
        if let Some(retransmission_timeout_id) = rsna_retransmission_timeout {
            self.rsna_retransmission_timeout.replace(retransmission_timeout_id);
        }

        // If the AP is responsive, then reset the response timeout.
        if let Some(response_timeout_id) = ap_responsive {
            self.rsna_response_timeout.replace(response_timeout_id);
        }
        self
    }

    /// Establish the RSNA if all conditions are met.
    fn try_establish(self, bss: &BssDescription, context: &mut Context) -> RsnaProgressed {
        if self.handshake_complete && self.pending_key_ids.is_empty() {
            context.mlme_sink.send(MlmeRequest::SetCtrlPort(fidl_mlme::SetControlledPortRequest {
                peer_sta_address: bss.bssid.0,
                state: fidl_mlme::ControlledPortState::Open,
            }));

            let now = now();
            RsnaProgressed::Complete(LinkUp { protection: Protection::Rsna(self.rsna), since: now })
        } else {
            RsnaProgressed::InProgress(self)
        }
    }

    fn handle_rsna_response_timeout(
        mut self,
        event_id: EventId,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        if !triggered(&self.rsna_response_timeout, event_id) {
            return Ok(self);
        };

        warn!("RSNA response timeout expired: {}ms", event::RSNA_RESPONSE_TIMEOUT_MILLIS);
        cancel(&mut self.rsna_retransmission_timeout);
        cancel(&mut self.rsna_response_timeout);
        cancel(&mut self.rsna_completion_timeout);
        Err(self.rsna.supplicant.on_rsna_response_timeout())
    }

    fn handle_rsna_completion_timeout(
        mut self,
        event_id: EventId,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        if !triggered(&self.rsna_completion_timeout, event_id) {
            return Ok(self);
        };

        warn!("RSNA completion timeout expired: {}ms", event::RSNA_COMPLETION_TIMEOUT_MILLIS);
        cancel(&mut self.rsna_retransmission_timeout);
        cancel(&mut self.rsna_response_timeout);
        cancel(&mut self.rsna_completion_timeout);
        Err(self.rsna.supplicant.on_rsna_completion_timeout())
    }
}

impl LinkState {
    pub fn new(
        protection: Protection,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        match protection {
            Protection::Rsna(mut rsna) | Protection::LegacyWpa(mut rsna) => {
                match rsna.supplicant.start() {
                    Ok(_) => Ok(State::new(Init)
                        .transition_to(EstablishingRsna {
                            rsna,
                            rsna_completion_timeout: Some(
                                context.timer.schedule(event::RsnaCompletionTimeout),
                            ),
                            rsna_response_timeout: Some(
                                context.timer.schedule(event::RsnaResponseTimeout),
                            ),
                            rsna_retransmission_timeout: None,
                            handshake_complete: false,
                            pending_key_ids: Default::default(),
                        })
                        .into()),
                    Err(e) => {
                        error!("could not start Supplicant: {}", e);
                        Err(EstablishRsnaFailureReason::StartSupplicantFailed)
                    }
                }
            }
            Protection::Open | Protection::Wep(_) => {
                let now = now();
                Ok(State::new(Init)
                    .transition_to(LinkUp { protection: Protection::Open, since: now })
                    .into())
            }
        }
    }

    pub fn disconnect(self) -> (Protection, Option<zx::Duration>) {
        match self {
            Self::EstablishingRsna(state) => {
                let (_, state) = state.release_data();
                (Protection::Rsna(state.rsna), None)
            }
            Self::LinkUp(state) => {
                let (_, state) = state.release_data();
                let connected_duration = now() - state.since;
                (state.protection, Some(connected_duration))
            }
            _ => unreachable!(),
        }
    }

    fn on_eapol_event<T, H>(
        self,
        eapol_event: T,
        process_eapol_event: H,
        bss: &BssDescription,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason>
    where
        H: Fn(&mut Context, &mut Rsna, &T) -> RsnaStatus,
    {
        match self {
            Self::EstablishingRsna(state) => {
                let (transition, mut state) = state.release_data();
                match process_eapol_event(context, &mut state.rsna, &eapol_event) {
                    RsnaStatus::Failed(failure_reason) => Err(failure_reason),
                    RsnaStatus::Progressed {
                        ap_responsive,
                        new_retransmission_timeout,
                        sent_keys,
                        handshake_complete,
                    } => {
                        match state
                            .on_rsna_progressed(
                                ap_responsive,
                                new_retransmission_timeout,
                                handshake_complete,
                                sent_keys,
                            )
                            .try_establish(bss, context)
                        {
                            RsnaProgressed::Complete(link_up) => {
                                state_change_msg.set_msg("RSNA established".to_string());
                                Ok(transition.to(link_up).into())
                            }
                            RsnaProgressed::InProgress(still_establishing_rsna) => {
                                Ok(transition.to(still_establishing_rsna).into())
                            }
                        }
                    }
                    RsnaStatus::Unchanged => Ok(transition.to(state).into()),
                }
            }
            Self::LinkUp(state) => {
                let (transition, mut state) = state.release_data();
                // Drop EAPOL frames if the BSS is not an RSN.
                if let Protection::Rsna(rsna) = &mut state.protection {
                    match process_eapol_event(context, rsna, &eapol_event) {
                        RsnaStatus::Unchanged => {}
                        // This can happen when there's a GTK rotation.
                        // Timeout is ignored because only one RX frame is
                        // needed in the exchange, so we are not waiting for
                        // another one.
                        // sent_keys is ignored because we're not waiting for key installations
                        // to complete the RSNA.
                        RsnaStatus::Progressed {
                            ap_responsive: _,
                            new_retransmission_timeout: _,
                            handshake_complete: _,
                            sent_keys: _,
                        } => {}
                        // Once re-keying is supported, the RSNA can fail in
                        // LinkUp as well and cause deauthentication.
                        s => error!("unexpected RsnaStatus in LinkUp state: {:?}", s),
                    };
                }
                Ok(transition.to(state).into())
            }
            _ => unreachable!(),
        }
    }

    pub fn on_eapol_ind(
        self,
        eapol_ind: fidl_mlme::EapolIndication,
        bss: &BssDescription,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        self.on_eapol_event(eapol_ind, process_eapol_ind, bss, state_change_msg, context)
    }

    pub fn on_eapol_conf(
        self,
        eapol_conf: fidl_mlme::EapolConfirm,
        bss: &BssDescription,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        self.on_eapol_event(eapol_conf, process_eapol_conf, bss, state_change_msg, context)
    }

    pub fn on_set_keys_conf(
        self,
        set_keys_conf: fidl_mlme::SetKeysConfirm,
        bss: &BssDescription,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        for key_result in &set_keys_conf.results {
            if key_result.status != zx::Status::OK.into_raw() {
                state_change_msg.set_msg("Failed to set key in driver".to_string());
                return Err(EstablishRsnaFailureReason::InternalError);
            }
        }

        match self {
            Self::EstablishingRsna(state) => {
                let (transition, mut state) = state.release_data();
                for key_result in set_keys_conf.results {
                    state.pending_key_ids.remove(&key_result.key_id);
                }
                match state.try_establish(bss, context) {
                    RsnaProgressed::Complete(link_up) => {
                        state_change_msg.set_msg("RSNA established".to_string());
                        Ok(transition.to(link_up).into())
                    }
                    RsnaProgressed::InProgress(still_establishing_rsna) => {
                        Ok(transition.to(still_establishing_rsna).into())
                    }
                }
            }
            _ => Ok(self),
        }
    }

    pub fn handle_timeout(
        self,
        event_id: EventId,
        event: Event,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailureReason> {
        match self {
            Self::EstablishingRsna(state) => match event {
                Event::RsnaResponseTimeout(RsnaResponseTimeout {}) => {
                    let (transition, state) = state.release_data();
                    match state.handle_rsna_response_timeout(event_id) {
                        Ok(still_establishing_rsna) => {
                            Ok(transition.to(still_establishing_rsna).into())
                        }
                        Err(failure) => {
                            state_change_msg.set_msg("RSNA response timeout".to_string());
                            Err(failure)
                        }
                    }
                }
                Event::RsnaCompletionTimeout(RsnaCompletionTimeout {}) => {
                    let (transition, state) = state.release_data();
                    match state.handle_rsna_completion_timeout(event_id) {
                        Ok(still_establishing_rsna) => {
                            Ok(transition.to(still_establishing_rsna).into())
                        }
                        Err(failure) => {
                            state_change_msg.set_msg("RSNA completion timeout".to_string());
                            Err(failure)
                        }
                    }
                }
                Event::RsnaRetransmissionTimeout(timeout) => {
                    let (transition, mut state) = state.release_data();
                    match process_rsna_retransmission_timeout(context, timeout, &mut state.rsna) {
                        RsnaStatus::Failed(failure_reason) => Err(failure_reason),
                        RsnaStatus::Unchanged => Ok(transition.to(state).into()),
                        RsnaStatus::Progressed {
                            ap_responsive,
                            new_retransmission_timeout,
                            sent_keys,
                            handshake_complete,
                        } => {
                            let still_establishing_rsna = state.on_rsna_progressed(
                                ap_responsive,
                                new_retransmission_timeout,
                                handshake_complete,
                                sent_keys,
                            );
                            Ok(transition.to(still_establishing_rsna).into())
                        }
                    }
                }
                _ => Ok(state.into()),
            },
            Self::LinkUp(state) => Ok(state.into()),
            _ => unreachable!(),
        }
    }
}

fn triggered(id: &Option<EventId>, received_id: EventId) -> bool {
    id.map_or(false, |id| id == received_id)
}

fn cancel(event_id: &mut Option<EventId>) {
    let _ = event_id.take();
}

fn inspect_log_key(context: &mut Context, key: &Key) {
    let (cipher, key_index) = match key {
        Key::Ptk(ptk) => (Some(&ptk.cipher), None),
        Key::Gtk(gtk) => (Some(&gtk.cipher), Some(gtk.key_id())),
        _ => (None, None),
    };
    inspect_log!(context.inspect.rsn_events.lock(), {
        derived_key: key.name(),
        cipher?: cipher.map(|c| format!("{:?}", c)),
        key_index?: key_index,
    });
}

fn send_keys(mlme_sink: &MlmeSink, bssid: Bssid, key: Key) -> Option<u16> {
    let key_descriptor = match key {
        Key::Ptk(ptk) => fidl_mlme::SetKeyDescriptor {
            key_type: fidl_mlme::KeyType::Pairwise,
            key: ptk.tk().to_vec(),
            key_id: 0,
            address: bssid.0,
            cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
            cipher_suite_type: ptk.cipher.suite_type,
            rsc: 0,
        },
        Key::Gtk(gtk) => fidl_mlme::SetKeyDescriptor {
            key_type: fidl_mlme::KeyType::Group,
            key: gtk.tk().to_vec(),
            key_id: gtk.key_id() as u16,
            address: WILDCARD_BSSID.0,
            cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
            cipher_suite_type: gtk.cipher.suite_type,
            rsc: gtk.rsc,
        },
        Key::Igtk(igtk) => {
            let mut rsc = [0u8; 8];
            rsc[2..].copy_from_slice(&igtk.ipn[..]);
            fidl_mlme::SetKeyDescriptor {
                key_type: fidl_mlme::KeyType::Igtk,
                key: igtk.igtk,
                key_id: igtk.key_id,
                address: [0xFFu8; 6],
                cipher_suite_oui: eapol::to_array(&igtk.cipher.oui[..]),
                cipher_suite_type: igtk.cipher.suite_type,
                rsc: u64::from_be_bytes(rsc),
            }
        }
        _ => {
            error!("derived unexpected key");
            return None;
        }
    };
    let key_id = key_descriptor.key_id;
    mlme_sink
        .send(MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest { keylist: vec![key_descriptor] }));
    return Some(key_id);
}

/// Sends an eapol frame, and optionally schedules a timeout for the response.
/// If schedule_timeout is true, we should expect our peer to send us an eapol
/// frame in response to this one, and schedule a timeout as well.
fn send_eapol_frame(
    context: &mut Context,
    bssid: Bssid,
    sta_addr: MacAddr,
    frame: eapol::KeyFrameBuf,
    schedule_timeout: bool,
) -> Option<EventId> {
    let resp_timeout_id = if schedule_timeout {
        Some(context.timer.schedule(event::RsnaRetransmissionTimeout { bssid, sta_addr }))
    } else {
        None
    };
    inspect_log!(context.inspect.rsn_events.lock(), tx_eapol_frame: InspectBytes(&frame[..]));
    context.mlme_sink.send(MlmeRequest::Eapol(fidl_mlme::EapolRequest {
        src_addr: sta_addr,
        dst_addr: bssid.0,
        data: frame.into(),
    }));
    resp_timeout_id
}

fn process_eapol_conf(
    context: &mut Context,
    rsna: &mut Rsna,
    eapol_conf: &fidl_mlme::EapolConfirm,
) -> RsnaStatus {
    let mut update_sink = rsna::UpdateSink::default();
    match rsna.supplicant.on_eapol_conf(&mut update_sink, eapol_conf.result_code) {
        Err(e) => {
            error!("error handling EAPOL confirm: {}", e);
            return RsnaStatus::Unchanged;
        }
        Ok(_) => {
            if update_sink.is_empty() {
                return RsnaStatus::Unchanged;
            }
        }
    }
    process_eapol_updates(context, Bssid(eapol_conf.dst_addr), update_sink, None)
}

fn process_rsna_retransmission_timeout(
    context: &mut Context,
    timeout: event::RsnaRetransmissionTimeout,
    rsna: &mut Rsna,
) -> RsnaStatus {
    let mut update_sink = rsna::UpdateSink::default();
    match rsna.supplicant.on_rsna_retransmission_timeout(&mut update_sink) {
        Err(e) => {
            error!("{:?}", e);
            return RsnaStatus::Failed(EstablishRsnaFailureReason::InternalError);
        }
        Ok(_) => {
            if update_sink.is_empty() {
                return RsnaStatus::Unchanged;
            }
        }
    }
    process_eapol_updates(context, timeout.bssid, update_sink, None)
}

fn process_eapol_ind(
    context: &mut Context,
    rsna: &mut Rsna,
    ind: &fidl_mlme::EapolIndication,
) -> RsnaStatus {
    let mic_size = rsna.negotiated_protection.mic_size;
    let eapol_pdu = &ind.data[..];
    let eapol_frame = match eapol::KeyFrameRx::parse(mic_size as usize, eapol_pdu) {
        Ok(key_frame) => eapol::Frame::Key(key_frame),
        Err(e) => {
            error!("received invalid EAPOL Key frame: {:?}", e);
            inspect_log!(context.inspect.rsn_events.lock(), {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: format!("rejected (parse error): {:?}", e)
            });
            return RsnaStatus::Unchanged;
        }
    };

    let mut update_sink = rsna::UpdateSink::default();
    match rsna.supplicant.on_eapol_frame(&mut update_sink, eapol_frame) {
        Err(e) => {
            error!("error processing EAPOL key frame: {}", e);
            inspect_log!(context.inspect.rsn_events.lock(), {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: format!("rejected (processing error): {}", e)
            });
            return RsnaStatus::Unchanged;
        }
        Ok(_) => {
            inspect_log!(context.inspect.rsn_events.lock(), {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: "processed"
            });
            if update_sink.is_empty() {
                return RsnaStatus::Unchanged;
            }
        }
    }
    let ap_responsive = Some(context.timer.schedule(event::RsnaResponseTimeout {}));
    process_eapol_updates(context, Bssid(ind.src_addr), update_sink, ap_responsive)
}

fn process_eapol_updates(
    context: &mut Context,
    bssid: Bssid,
    updates: rsna::UpdateSink,
    ap_responsive: Option<EventId>,
) -> RsnaStatus {
    let sta_addr = context.device_info.sta_addr;
    let mut new_retransmission_timeout = None;
    let mut handshake_complete = false;
    let mut sent_keys = vec![];
    for update in updates {
        match update {
            // ESS Security Association requests to send an EAPOL frame.
            // Forward EAPOL frame to MLME.
            SecAssocUpdate::TxEapolKeyFrame { frame, expect_response } => {
                new_retransmission_timeout =
                    send_eapol_frame(context, bssid, sta_addr, frame, expect_response);
            }
            // ESS Security Association derived a new key.
            // Configure key in MLME.
            SecAssocUpdate::Key(key) => {
                inspect_log_key(context, &key);
                if let Some(key_id) = send_keys(&context.mlme_sink, bssid, key) {
                    sent_keys.push(key_id);
                }
            }
            // Received a status update.
            SecAssocUpdate::Status(status) => {
                inspect_log!(
                    context.inspect.rsn_events.lock(),
                    rsna_status: format!("{:?}", status)
                );
                match status {
                    // ESS Security Association was successfully established. Link is now up.
                    SecAssocStatus::EssSaEstablished => {
                        handshake_complete = true;
                    }
                    SecAssocStatus::WrongPassword => {
                        return RsnaStatus::Failed(EstablishRsnaFailureReason::InternalError);
                    }
                    SecAssocStatus::PmkSaEstablished => (),
                }
            }
            // TODO(fxbug.dev/29105): We must handle SAE here for FullMAC devices.
            update => warn!("Unhandled association update: {:?}", update),
        }
    }

    RsnaStatus::Progressed {
        ap_responsive,
        new_retransmission_timeout,
        handshake_complete,
        sent_keys,
    }
}
