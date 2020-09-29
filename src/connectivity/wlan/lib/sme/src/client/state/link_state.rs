// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{now, Protection, StateChangeContext, StateChangeContextExt},
    crate::{
        client::{
            event::{self, Event},
            info::ConnectionPingInfo,
            internal::Context,
            rsn::Rsna,
            EstablishRsnaFailure,
        },
        sink::MlmeSink,
        timer::EventId,
        MlmeRequest,
    },
    anyhow,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription},
    fuchsia_inspect_contrib::{inspect_log, log::InspectBytes},
    fuchsia_zircon as zx,
    log::{error, warn},
    wlan_rsn::{
        key::exchange::Key,
        rsna::{self, SecAssocStatus, SecAssocUpdate},
    },
    wlan_statemachine::*,
};

#[derive(Debug)]
pub struct Init;

#[derive(Debug)]
pub struct EstablishingRsna {
    pub rsna: Rsna,
    // Timeout for the total duration RSNA may take to complete.
    pub rsna_timeout: Option<EventId>,
    // Timeout waiting to receive a key frame from the Authenticator. This timeout is None at
    // the beginning of the RSNA when no frame has been exchanged yet, or at the end of the
    // RSNA when all the key frames have finished exchanging.
    pub resp_timeout: Option<EventId>,
}

#[derive(Debug)]
pub struct LinkUp {
    pub protection: Protection,
    pub since: zx::Time,
    pub ping_event: Option<EventId>,
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
    Established,
    Failed(EstablishRsnaFailure),
    Unchanged,
    Progressed { new_resp_timeout: Option<EventId> },
}

impl EstablishingRsna {
    fn on_rsna_established(self, bss: &BssDescription, context: &mut Context) -> LinkUp {
        context.mlme_sink.send(MlmeRequest::SetCtrlPort(fidl_mlme::SetControlledPortRequest {
            peer_sta_address: bss.bssid.clone(),
            state: fidl_mlme::ControlledPortState::Open,
        }));

        let now = now();
        let info = ConnectionPingInfo::first_connected(now);
        let ping_event = Some(report_ping(info, context));
        LinkUp { protection: Protection::Rsna(self.rsna), since: now, ping_event }
    }

    fn on_rsna_progressed(mut self, new_resp_timeout: Option<EventId>) -> Self {
        cancel(&mut self.resp_timeout);
        if let Some(id) = new_resp_timeout {
            self.resp_timeout.replace(id);
        }
        self
    }

    fn handle_establishing_rsna_timeout(
        mut self,
        event_id: EventId,
    ) -> Result<Self, EstablishRsnaFailure> {
        if !triggered(&self.rsna_timeout, event_id) {
            return Ok(self);
        }

        error!("timeout establishing RSNA");
        cancel(&mut self.rsna_timeout);
        Err(EstablishRsnaFailure::OverallTimeout)
    }

    fn handle_key_frame_exchange_timeout(
        mut self,
        timeout: event::KeyFrameExchangeTimeout,
        event_id: EventId,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailure> {
        if triggered(&self.resp_timeout, event_id) {
            if timeout.attempt < event::KEY_FRAME_EXCHANGE_MAX_ATTEMPTS {
                warn!("timeout waiting for key frame for attempt {}; retrying", timeout.attempt);
                let id = send_eapol_frame(
                    context,
                    timeout.bssid,
                    timeout.sta_addr,
                    timeout.frame,
                    timeout.attempt + 1,
                );
                self.resp_timeout.replace(id);
                Ok(self)
            } else {
                error!("timeout waiting for key frame for last attempt; deauth");
                cancel(&mut self.resp_timeout);
                Err(EstablishRsnaFailure::KeyFrameExchangeTimeout)
            }
        } else {
            Ok(self)
        }
    }
}

impl LinkUp {
    pub fn connected_duration(&self) -> zx::Duration {
        now() - self.since
    }

    fn handle_connection_ping(
        &mut self,
        event_id: EventId,
        prev_ping: ConnectionPingInfo,
        context: &mut Context,
    ) {
        if triggered(&self.ping_event, event_id) {
            cancel(&mut self.ping_event);
            self.ping_event.replace(report_ping(prev_ping.next_ping(now()), context));
        }
    }
}

impl LinkState {
    pub fn new(
        protection: Protection,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailure> {
        match protection {
            Protection::Rsna(mut rsna) | Protection::LegacyWpa(mut rsna) => {
                context.info.report_rsna_started(context.att_id);
                match rsna.supplicant.start() {
                    Ok(_) => {
                        let rsna_timeout =
                            Some(context.timer.schedule(event::EstablishingRsnaTimeout));
                        Ok(State::new(Init)
                            .transition_to(EstablishingRsna {
                                rsna,
                                rsna_timeout,
                                resp_timeout: None,
                            })
                            .into())
                    }
                    Err(e) => {
                        error!("could not start Supplicant: {}", e);
                        context.info.report_supplicant_error(anyhow::anyhow!(e));
                        Err(EstablishRsnaFailure::StartSupplicantFailed)
                    }
                }
            }
            Protection::Open | Protection::Wep(_) => {
                let now = now();
                let info = ConnectionPingInfo::first_connected(now);
                let ping_event = Some(report_ping(info, context));
                Ok(State::new(Init)
                    .transition_to(LinkUp { protection: Protection::Open, since: now, ping_event })
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

    pub fn on_eapol_ind(
        self,
        ind: fidl_mlme::EapolIndication,
        bss: &BssDescription,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailure> {
        match self {
            Self::EstablishingRsna(state) => {
                let (transition, mut state) = state.release_data();
                match process_eapol_ind(context, &mut state.rsna, &ind) {
                    RsnaStatus::Established => {
                        let link_up = state.on_rsna_established(bss, context);
                        state_change_msg.set_msg("RSNA established".to_string());
                        Ok(transition.to(link_up).into())
                    }
                    RsnaStatus::Failed(failure_reason) => Err(failure_reason),
                    RsnaStatus::Progressed { new_resp_timeout } => {
                        let still_establishing_rsna = state.on_rsna_progressed(new_resp_timeout);
                        Ok(transition.to(still_establishing_rsna).into())
                    }
                    RsnaStatus::Unchanged => Ok(transition.to(state).into()),
                }
            }
            Self::LinkUp(state) => {
                let (transition, mut state) = state.release_data();
                // Drop EAPOL frames if the BSS is not an RSN.
                if let Protection::Rsna(rsna) = &mut state.protection {
                    match process_eapol_ind(context, rsna, &ind) {
                        RsnaStatus::Unchanged => {}
                        // This can happen when there's a GTK rotation.
                        // Timeout is ignored because only one RX frame is
                        // needed in the exchange, so we are not waiting for
                        // another one.
                        RsnaStatus::Progressed { new_resp_timeout: _ } => {}
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

    pub fn handle_timeout(
        self,
        event_id: EventId,
        event: Event,
        state_change_msg: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, EstablishRsnaFailure> {
        match self {
            Self::EstablishingRsna(state) => match event {
                Event::EstablishingRsnaTimeout(..) => {
                    let (transition, state) = state.release_data();
                    match state.handle_establishing_rsna_timeout(event_id) {
                        Ok(still_establishing_rsna) => {
                            Ok(transition.to(still_establishing_rsna).into())
                        }
                        Err(failure) => {
                            state_change_msg.set_msg("RSNA timeout".to_string());
                            Err(failure)
                        }
                    }
                }
                Event::KeyFrameExchangeTimeout(timeout) => {
                    let (transition, state) = state.release_data();
                    context.info.report_key_exchange_timeout();
                    match state.handle_key_frame_exchange_timeout(timeout, event_id, context) {
                        Ok(still_establishing_rsna) => {
                            Ok(transition.to(still_establishing_rsna).into())
                        }
                        Err(failure) => {
                            state_change_msg.set_msg("key frame rx timeout".to_string());
                            Err(failure)
                        }
                    }
                }
                _ => Ok(state.into()),
            },
            Self::LinkUp(mut state) => match event {
                Event::ConnectionPing(prev_ping) => {
                    state.handle_connection_ping(event_id, prev_ping, context);
                    Ok(state.into())
                }
                _ => Ok(state.into()),
            },
            _ => unreachable!(),
        }
    }
}

fn report_ping(info: ConnectionPingInfo, context: &mut Context) -> EventId {
    context.info.report_connection_ping(info.clone());
    context.timer.schedule(info)
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

fn send_keys(mlme_sink: &MlmeSink, bssid: [u8; 6], key: Key) {
    match key {
        Key::Ptk(ptk) => {
            mlme_sink.send(MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest {
                keylist: vec![fidl_mlme::SetKeyDescriptor {
                    key_type: fidl_mlme::KeyType::Pairwise,
                    key: ptk.tk().to_vec(),
                    key_id: 0,
                    address: bssid,
                    cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
                    cipher_suite_type: ptk.cipher.suite_type,
                    rsc: 0,
                }],
            }));
        }
        Key::Gtk(gtk) => {
            mlme_sink.send(MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest {
                keylist: vec![fidl_mlme::SetKeyDescriptor {
                    key_type: fidl_mlme::KeyType::Group,
                    key: gtk.tk().to_vec(),
                    key_id: gtk.key_id() as u16,
                    address: [0xFFu8; 6],
                    cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
                    cipher_suite_type: gtk.cipher.suite_type,
                    rsc: gtk.rsc,
                }],
            }));
        }
        _ => error!("derived unexpected key"),
    };
}

fn send_eapol_frame(
    context: &mut Context,
    bssid: [u8; 6],
    sta_addr: [u8; 6],
    frame: eapol::KeyFrameBuf,
    attempt: u32,
) -> EventId {
    let resp_timeout_id = context.timer.schedule(event::KeyFrameExchangeTimeout {
        bssid,
        sta_addr,
        frame: frame.clone(),
        attempt,
    });

    inspect_log!(context.inspect.rsn_events.lock(), tx_eapol_frame: InspectBytes(&frame[..]));
    context.mlme_sink.send(MlmeRequest::Eapol(fidl_mlme::EapolRequest {
        src_addr: sta_addr,
        dst_addr: bssid,
        data: frame.into(),
    }));
    resp_timeout_id
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
            context.info.report_supplicant_error(anyhow::anyhow!(e));
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
    context.info.report_supplicant_updates(&update_sink);

    let bssid = ind.src_addr;
    let sta_addr = ind.dst_addr;
    let mut new_resp_timeout = None;
    for update in update_sink {
        match update {
            // ESS Security Association requests to send an EAPOL frame.
            // Forward EAPOL frame to MLME.
            SecAssocUpdate::TxEapolKeyFrame(frame) => {
                new_resp_timeout.replace(send_eapol_frame(context, bssid, sta_addr, frame, 1));
            }
            // ESS Security Association derived a new key.
            // Configure key in MLME.
            SecAssocUpdate::Key(key) => {
                inspect_log_key(context, &key);
                send_keys(&context.mlme_sink, bssid, key)
            }
            // Received a status update.
            // TODO(hahnr): Rework this part.
            // As of now, we depend on the fact that the status is always the last update.
            // However, this fact is not clear from the API.
            // We should fix the API and make this more explicit.
            // Then we should rework this part.
            SecAssocUpdate::Status(status) => {
                inspect_log!(
                    context.inspect.rsn_events.lock(),
                    rsna_status: format!("{:?}", status)
                );
                match status {
                    // ESS Security Association was successfully established. Link is now up.
                    SecAssocStatus::EssSaEstablished => return RsnaStatus::Established,
                    SecAssocStatus::WrongPassword => {
                        return RsnaStatus::Failed(EstablishRsnaFailure::InternalError)
                    }
                    _ => (),
                }
            }
            // TODO(fxbug.dev/29105): We must handle SAE here for FullMAC devices.
            update => warn!("Unhandled association update: {:?}", update),
        }
    }

    RsnaStatus::Progressed { new_resp_timeout }
}
