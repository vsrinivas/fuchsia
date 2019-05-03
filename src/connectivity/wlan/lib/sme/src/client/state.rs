// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription, MlmeEvent};
use log::{error, warn};
use wlan_common::{format::MacFmt, RadioConfig};
use wlan_inspect::{inspect_log, log::InspectBytes};
use wlan_rsn::key::exchange::Key;
use wlan_rsn::rsna::{self, SecAssocStatus, SecAssocUpdate};

use super::bss::convert_bss_description;
use super::rsn::Rsna;
use super::{ConnectFailure, ConnectResult, InfoEvent, Status};

use crate::client::{
    event::{self, Event},
    report_connect_finished, Context,
};
use crate::clone_utils::clone_bss_desc;
use crate::phy_selection::derive_phy_cbw;
use crate::responder::Responder;
use crate::sink::MlmeSink;
use crate::timer::EventId;
use crate::MlmeRequest;

const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

const IDLE_STATE: &str = "IdleState";
const JOINING_STATE: &str = "JoiningState";
const AUTHENTICATING_STATE: &str = "AuthenticatingState";
const ASSOCIATING_STATE: &str = "AssociatingState";
const RSNA_STATE: &str = "EstablishingRsnaState";
const LINK_UP_STATE: &str = "LinkUpState";

#[derive(Debug)]
pub enum LinkState {
    EstablishingRsna {
        responder: Option<Responder<ConnectResult>>,
        rsna: Rsna,
        // Timeout for the total duration RSNA may take to complete.
        rsna_timeout: Option<EventId>,
        // Timeout waiting to receive a key frame from the Authenticator. This timeout is None at
        // the beginning of the RSNA when no frame has been exchanged yet, or at the end of the
        // RSNA when all the key frames have finished exchanging.
        resp_timeout: Option<EventId>,
    },
    LinkUp {
        protection: Protection,
    },
}

#[derive(Debug)]
pub enum Protection {
    Open,
    Rsna(Rsna),
}

#[derive(Debug)]
pub struct ConnectCommand {
    pub bss: Box<BssDescription>,
    pub responder: Option<Responder<ConnectResult>>,
    pub protection: Protection,
    pub radio_cfg: RadioConfig,
}

#[derive(Debug)]
pub enum RsnaStatus {
    Established,
    Failed(ConnectResult),
    Unchanged,
    Progressed { new_resp_timeout: Option<EventId> },
}

#[derive(Debug)]
pub enum State {
    Idle,
    Joining {
        cmd: ConnectCommand,
    },
    Authenticating {
        cmd: ConnectCommand,
    },
    Associating {
        cmd: ConnectCommand,
    },
    Associated {
        bss: Box<BssDescription>,
        last_rssi: Option<i8>,
        link_state: LinkState,
        radio_cfg: RadioConfig,
    },
}

impl State {
    fn state_name(&self) -> &'static str {
        match self {
            State::Idle => IDLE_STATE,
            State::Joining { .. } => JOINING_STATE,
            State::Authenticating { .. } => AUTHENTICATING_STATE,
            State::Associating { .. } => ASSOCIATING_STATE,
            State::Associated { link_state, .. } => match link_state {
                LinkState::EstablishingRsna { .. } => RSNA_STATE,
                LinkState::LinkUp { .. } => LINK_UP_STATE,
            },
        }
    }

    pub fn on_mlme_event(self, event: MlmeEvent, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_msg: Option<String> = None;

        let new_state = match self {
            State::Idle => {
                warn!("Unexpected MLME message while Idle: {:?}", event);
                State::Idle
            }
            State::Joining { cmd } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        context.mlme_sink.send(MlmeRequest::Authenticate(
                            fidl_mlme::AuthenticateRequest {
                                peer_sta_address: cmd.bss.bssid.clone(),
                                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                            },
                        ));
                        state_change_msg.replace("successful join".to_string());
                        State::Authenticating { cmd }
                    }
                    other => {
                        error!("Join request failed with result code {:?}", other);
                        report_connect_finished(
                            cmd.responder,
                            &context,
                            ConnectResult::Failed,
                            Some(ConnectFailure::JoinFailure(other)),
                        );
                        state_change_msg.replace(format!("failed join; result code: {:?}", other));
                        State::Idle
                    }
                },
                _ => State::Joining { cmd },
            },
            State::Authenticating { cmd } => match event {
                MlmeEvent::AuthenticateConf { resp } => match resp.result_code {
                    fidl_mlme::AuthenticateResultCodes::Success => {
                        state_change_msg.replace("successful auth".to_string());
                        to_associating_state(cmd, &context.mlme_sink)
                    }
                    other => {
                        error!("Authenticate request failed with result code {:?}", other);
                        report_connect_finished(
                            cmd.responder,
                            &context,
                            ConnectResult::Failed,
                            Some(ConnectFailure::AuthenticationFailure(other)),
                        );
                        state_change_msg.replace(format!("failed auth; result code: {:?}", other));
                        State::Idle
                    }
                },
                _ => State::Authenticating { cmd },
            },
            State::Associating { cmd } => match event {
                MlmeEvent::AssociateConf { resp } => {
                    handle_mlme_assoc_conf(resp, cmd, context, &mut state_change_msg)
                }
                _ => State::Associating { cmd },
            },
            State::Associated { bss, last_rssi, link_state, radio_cfg } => match event {
                MlmeEvent::DisassociateInd { .. } => {
                    let (responder, mut protection) = match link_state {
                        LinkState::LinkUp { protection } => (None, protection),
                        LinkState::EstablishingRsna { responder, rsna, .. } => {
                            (responder, Protection::Rsna(rsna))
                        }
                    };
                    // Client is disassociating. The ESS-SA must be kept alive but reset.
                    if let Protection::Rsna(rsna) = &mut protection {
                        rsna.supplicant.reset();
                    }

                    let cmd = ConnectCommand { bss, responder, protection, radio_cfg };
                    context.att_id += 1;
                    state_change_msg.replace("received DisassociateInd msg".to_string());
                    to_associating_state(cmd, &context.mlme_sink)
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    if let LinkState::EstablishingRsna { responder, .. } = link_state {
                        let connect_result = deauth_code_to_connect_result(ind.reason_code);
                        report_connect_finished(responder, &context, connect_result, None);
                    }
                    state_change_msg.replace("received DeauthenticateInd msg".to_string());
                    State::Idle
                }
                MlmeEvent::SignalReport { ind } => {
                    State::Associated { bss, last_rssi: Some(ind.rssi_dbm), link_state, radio_cfg }
                }
                MlmeEvent::EapolInd { ref ind } if bss.rsn.is_some() => {
                    // Reject EAPOL frames from other BSS.
                    if ind.src_addr != bss.bssid {
                        let eapol_pdu = &ind.data[..];
                        inspect_log!(context.inspect.rsn_events, {
                            rx_eapol_frame: InspectBytes(&eapol_pdu),
                            foreign_bssid: ind.src_addr.to_mac_str(),
                            current_bssid: bss.bssid.to_mac_str(),
                            status: "rejected (foreign BSS)",
                        });
                        return State::Associated { bss, last_rssi, link_state, radio_cfg };
                    }

                    match link_state {
                        LinkState::EstablishingRsna {
                            responder,
                            mut rsna,
                            rsna_timeout,
                            mut resp_timeout,
                        } => match process_eapol_ind(context, &mut rsna, &ind) {
                            RsnaStatus::Established => {
                                context.mlme_sink.send(MlmeRequest::SetCtrlPort(
                                    fidl_mlme::SetControlledPortRequest {
                                        peer_sta_address: bss.bssid.clone(),
                                        state: fidl_mlme::ControlledPortState::Open,
                                    },
                                ));
                                context
                                    .info_sink
                                    .send(InfoEvent::RsnaEstablished { att_id: context.att_id });
                                report_connect_finished(
                                    responder,
                                    &context,
                                    ConnectResult::Success,
                                    None,
                                );
                                state_change_msg.replace("RSNA established".to_string());
                                let link_state =
                                    LinkState::LinkUp { protection: Protection::Rsna(rsna) };
                                State::Associated { bss, last_rssi, link_state, radio_cfg }
                            }
                            RsnaStatus::Failed(result) => {
                                report_connect_finished(responder, &context, result, None);
                                send_deauthenticate_request(bss, &context.mlme_sink);
                                state_change_msg.replace("RSNA failed".to_string());
                                State::Idle
                            }
                            RsnaStatus::Unchanged => {
                                let link_state = LinkState::EstablishingRsna {
                                    responder,
                                    rsna,
                                    rsna_timeout,
                                    resp_timeout,
                                };
                                State::Associated { bss, last_rssi, link_state, radio_cfg }
                            }
                            RsnaStatus::Progressed { new_resp_timeout } => {
                                cancel(&mut resp_timeout);
                                if let Some(id) = new_resp_timeout {
                                    resp_timeout.replace(id);
                                }
                                let link_state = LinkState::EstablishingRsna {
                                    responder,
                                    rsna,
                                    rsna_timeout,
                                    resp_timeout,
                                };
                                State::Associated { bss, last_rssi, link_state, radio_cfg }
                            }
                        },
                        LinkState::LinkUp { protection } => match protection {
                            Protection::Rsna(mut rsna) => {
                                match process_eapol_ind(context, &mut rsna, &ind) {
                                    RsnaStatus::Unchanged => {}
                                    // This can happen when there's a GTK rotation.
                                    // Timeout is ignored because only one RX frame is needed in
                                    // the exchange, so we are not waiting for another one.
                                    RsnaStatus::Progressed { new_resp_timeout: _ } => {}
                                    // Once re-keying is supported, the RSNA can fail in LinkUp as
                                    // well and cause deauthentication.
                                    s => error!("unexpected RsnaStatus in LinkUp state: {:?}", s),
                                };
                                let link_state =
                                    LinkState::LinkUp { protection: Protection::Rsna(rsna) };
                                State::Associated { bss, last_rssi, link_state, radio_cfg }
                            }
                            // Drop EAPOL frames if the BSS is not an RSN.
                            _ => {
                                let link_state = LinkState::LinkUp { protection };
                                State::Associated { bss, last_rssi, link_state, radio_cfg }
                            }
                        },
                    }
                }
                _ => State::Associated { bss, last_rssi, link_state, radio_cfg },
            },
        };

        if start_state != new_state.state_name() || state_change_msg.is_some() {
            inspect_log!(context.inspect.states, {
                from: start_state,
                to: new_state.state_name(),
                ctx: state_change_msg,
            });
        }
        new_state
    }

    pub fn handle_timeout(self, event_id: EventId, event: Event, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_msg: Option<String> = None;

        let new_state = match self {
            State::Associated { bss, last_rssi, link_state, radio_cfg } => match link_state {
                LinkState::EstablishingRsna {
                    responder,
                    rsna,
                    mut rsna_timeout,
                    mut resp_timeout,
                } => match event {
                    Event::EstablishingRsnaTimeout if triggered(&rsna_timeout, event_id) => {
                        error!("timeout establishing RSNA; deauthenticating");
                        cancel(&mut rsna_timeout);
                        report_connect_finished(
                            responder,
                            &context,
                            ConnectResult::Failed,
                            Some(ConnectFailure::RsnaTimeout),
                        );
                        send_deauthenticate_request(bss, &context.mlme_sink);
                        state_change_msg.replace("RSNA timeout".to_string());
                        State::Idle
                    }
                    Event::KeyFrameExchangeTimeout { bssid, sta_addr, frame, attempt } => {
                        if !triggered(&resp_timeout, event_id) {
                            let link_state = LinkState::EstablishingRsna {
                                responder,
                                rsna,
                                rsna_timeout,
                                resp_timeout,
                            };
                            return State::Associated { bss, last_rssi, link_state, radio_cfg };
                        }

                        if attempt < event::KEY_FRAME_EXCHANGE_MAX_ATTEMPTS {
                            warn!(
                                "timeout waiting for key frame for attempt {}; retrying",
                                attempt
                            );
                            let id = send_eapol_frame(context, bssid, sta_addr, frame, attempt + 1);
                            resp_timeout.replace(id);
                            let link_state = LinkState::EstablishingRsna {
                                responder,
                                rsna,
                                rsna_timeout,
                                resp_timeout,
                            };
                            State::Associated { bss, last_rssi, link_state, radio_cfg }
                        } else {
                            error!("timeout waiting for key frame for last attempt; deauth");
                            cancel(&mut resp_timeout);
                            report_connect_finished(
                                responder,
                                &context,
                                ConnectResult::Failed,
                                Some(ConnectFailure::RsnaTimeout),
                            );
                            send_deauthenticate_request(bss, &context.mlme_sink);
                            state_change_msg.replace("key frame rx timeout".to_string());
                            State::Idle
                        }
                    }
                    _ => {
                        let link_state = LinkState::EstablishingRsna {
                            responder,
                            rsna,
                            rsna_timeout,
                            resp_timeout,
                        };
                        State::Associated { bss, last_rssi, link_state, radio_cfg }
                    }
                },
                _ => State::Associated { bss, last_rssi, link_state, radio_cfg },
            },
            _ => self,
        };

        if start_state != new_state.state_name() || state_change_msg.is_some() {
            inspect_log!(context.inspect.states, {
                from: start_state,
                to: new_state.state_name(),
                ctx: state_change_msg,
            });
        }
        new_state
    }

    pub fn connect(self, cmd: ConnectCommand, context: &mut Context) -> Self {
        let start_state = self.state_name();
        self.disconnect_internal(context);

        let mut selected_bss = clone_bss_desc(&cmd.bss);
        let (phy_to_use, cbw_to_use) =
            derive_phy_cbw(&selected_bss, &context.device_info, &cmd.radio_cfg);
        selected_bss.chan.cbw = cbw_to_use;

        context.mlme_sink.send(MlmeRequest::Join(fidl_mlme::JoinRequest {
            selected_bss,
            join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
            nav_sync_delay: 0,
            op_rate_set: vec![],
            phy: phy_to_use,
            cbw: cbw_to_use,
        }));
        context.att_id += 1;
        context.info_sink.send(InfoEvent::AssociationStarted { att_id: context.att_id });

        let msg = connect_cmd_inspect_summary(&cmd);
        inspect_log!(context.inspect.states, from: start_state, to: JOINING_STATE, ctx: msg);
        State::Joining { cmd }
    }

    pub fn disconnect(self, context: &mut Context) -> Self {
        inspect_log!(context.inspect.states, {
            from: self.state_name(),
            to: IDLE_STATE,
            ctx: "disconnect command",
        });
        self.disconnect_internal(context);
        State::Idle
    }

    fn disconnect_internal(self, context: &Context) {
        match self {
            State::Idle => {}
            State::Joining { cmd } | State::Authenticating { cmd } => {
                report_connect_finished(cmd.responder, &context, ConnectResult::Canceled, None);
            }
            State::Associating { cmd, .. } => {
                report_connect_finished(cmd.responder, &context, ConnectResult::Canceled, None);
                send_deauthenticate_request(cmd.bss, &context.mlme_sink);
            }
            State::Associated { bss, .. } => {
                send_deauthenticate_request(bss, &context.mlme_sink);
            }
        }
    }

    pub fn status(&self) -> Status {
        match self {
            State::Idle => Status { connected_to: None, connecting_to: None },
            State::Joining { cmd }
            | State::Authenticating { cmd }
            | State::Associating { cmd, .. } => {
                Status { connected_to: None, connecting_to: Some(cmd.bss.ssid.clone()) }
            }
            State::Associated { bss, link_state: LinkState::EstablishingRsna { .. }, .. } => {
                Status { connected_to: None, connecting_to: Some(bss.ssid.clone()) }
            }
            State::Associated { bss, link_state: LinkState::LinkUp { .. }, .. } => {
                Status { connected_to: Some(convert_bss_description(bss)), connecting_to: None }
            }
        }
    }
}

fn handle_mlme_assoc_conf(
    resp: fidl_mlme::AssociateConfirm,
    cmd: ConnectCommand,
    context: &mut Context,
    state_change_msg: &mut Option<String>,
) -> State {
    match resp.result_code {
        fidl_mlme::AssociateResultCodes::Success => {
            context.info_sink.send(InfoEvent::AssociationSuccess { att_id: context.att_id });
            match cmd.protection {
                Protection::Rsna(mut rsna) => match rsna.supplicant.start() {
                    Err(e) => {
                        handle_supplicant_start_failure(cmd.responder, cmd.bss, &context, e);
                        state_change_msg.replace("supplicant failed to start".to_string());
                        State::Idle
                    }
                    Ok(_) => {
                        context.info_sink.send(InfoEvent::RsnaStarted { att_id: context.att_id });

                        let rsna_timeout =
                            Some(context.timer.schedule(Event::EstablishingRsnaTimeout));
                        state_change_msg.replace("successful association".to_string());
                        State::Associated {
                            bss: cmd.bss,
                            last_rssi: None,
                            link_state: LinkState::EstablishingRsna {
                                responder: cmd.responder,
                                rsna,
                                rsna_timeout,
                                resp_timeout: None,
                            },
                            radio_cfg: cmd.radio_cfg,
                        }
                    }
                },
                Protection::Open => {
                    report_connect_finished(cmd.responder, &context, ConnectResult::Success, None);
                    state_change_msg.replace("successful association".to_string());
                    State::Associated {
                        bss: cmd.bss,
                        last_rssi: None,
                        link_state: LinkState::LinkUp { protection: Protection::Open },
                        radio_cfg: cmd.radio_cfg,
                    }
                }
            }
        }
        other => {
            error!("Associate request failed with result code {:?}", other);
            report_connect_finished(
                cmd.responder,
                &context,
                ConnectResult::Failed,
                Some(ConnectFailure::AssociationFailure(other)),
            );
            state_change_msg.replace(format!("failed association; result code: {:?}", other));
            State::Idle
        }
    }
}

/// Custom logging for ConnectCommand because its normal full debug string is too large, and we
/// want to reduce how much we log in memory for Inspect. Additionally, in the future, we'd need
/// to anonymize information like BSSID and SSID.
fn connect_cmd_inspect_summary(cmd: &ConnectCommand) -> String {
    let bss = &cmd.bss;
    let ssid = match String::from_utf8(bss.ssid.clone()) {
        Ok(ssid) => ssid,
        Err(_) => format!("{:?}", bss.ssid),
    };
    format!(
        "ConnectCmd {{ \
         bssid: {bssid}, ssid: {ssid:?}, cap: {cap:?}, basic_rate_set: {basic_rate_set:?}, \
         op_rate_set: {op_rate_set:?}, protected: {protected:?}, chan: {chan:?}, \
         rcpi: {rcpi:?}, rsni: {rsni:?}, rssi: {rssi:?}, ht_cap: {ht_cap:?}, ht_op: {ht_op:?}, \
         vht_cap: {vht_cap:?}, vht_op: {vht_op:?} }}",
        bssid = bss.bssid.to_mac_str(),
        ssid = ssid,
        cap = bss.cap,
        basic_rate_set = bss.basic_rate_set,
        op_rate_set = bss.op_rate_set,
        protected = bss.rsn.is_some(),
        chan = bss.chan,
        rcpi = bss.rcpi_dbmh,
        rsni = bss.rsni_dbh,
        rssi = bss.rssi_dbm,
        ht_cap = bss.ht_cap.is_some(),
        ht_op = bss.ht_op.is_some(),
        vht_cap = bss.vht_cap.is_some(),
        vht_op = bss.vht_op.is_some()
    )
}

fn triggered(id: &Option<EventId>, received_id: EventId) -> bool {
    id.map_or(false, |id| id == received_id)
}

fn cancel(event_id: &mut Option<EventId>) {
    let _ = event_id.take();
}

fn deauth_code_to_connect_result(reason_code: fidl_mlme::ReasonCode) -> ConnectResult {
    match reason_code {
        fidl_mlme::ReasonCode::InvalidAuthentication
        | fidl_mlme::ReasonCode::Ieee8021XAuthFailed => ConnectResult::BadCredentials,
        _ => ConnectResult::Failed,
    }
}

fn process_eapol_ind(
    context: &mut Context,
    rsna: &mut Rsna,
    ind: &fidl_mlme::EapolIndication,
) -> RsnaStatus {
    let mic_size = rsna.negotiated_rsne.mic_size;
    let eapol_pdu = &ind.data[..];
    let eapol_frame = match eapol::key_frame_from_bytes(eapol_pdu, mic_size).to_full_result() {
        Ok(key_frame) => eapol::Frame::Key(key_frame),
        Err(e) => {
            error!("received invalid EAPOL Key frame: {:?}", e);
            inspect_log!(context.inspect.rsn_events, {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: format!("rejected (parse error): {:?}", e)
            });
            return RsnaStatus::Unchanged;
        }
    };

    let mut update_sink = rsna::UpdateSink::default();
    match rsna.supplicant.on_eapol_frame(&mut update_sink, &eapol_frame) {
        Err(e) => {
            error!("error processing EAPOL key frame: {}", e);
            inspect_log!(context.inspect.rsn_events, {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: format!("rejected (processing error): {}", e)
            });
            return RsnaStatus::Unchanged;
        }
        Ok(_) => {
            inspect_log!(context.inspect.rsn_events, {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                status: "processed"
            });
            if update_sink.is_empty() {
                return RsnaStatus::Unchanged;
            }
        }
    }

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
                inspect_log!(context.inspect.rsn_events, derived_key: key.name());
                send_keys(&context.mlme_sink, bssid, key)
            }
            // Received a status update.
            // TODO(hahnr): Rework this part.
            // As of now, we depend on the fact that the status is always the last update.
            // However, this fact is not clear from the API.
            // We should fix the API and make this more explicit.
            // Then we should rework this part.
            SecAssocUpdate::Status(status) => {
                inspect_log!(context.inspect.rsn_events, rsna_status: format!("{:?}", status));
                match status {
                    // ESS Security Association was successfully established. Link is now up.
                    SecAssocStatus::EssSaEstablished => return RsnaStatus::Established,
                    // TODO(hahnr): The API should not expose whether or not the connection failed
                    // because of bad credentials as it allows callers to reason about location
                    // information since the network was apparently found.
                    SecAssocStatus::WrongPassword => {
                        return RsnaStatus::Failed(ConnectResult::BadCredentials);
                    }
                }
            }
        }
    }

    RsnaStatus::Progressed { new_resp_timeout }
}

fn send_eapol_frame(
    context: &mut Context,
    bssid: [u8; 6],
    sta_addr: [u8; 6],
    frame: eapol::KeyFrame,
    attempt: u32,
) -> EventId {
    let resp_timeout_id = context.timer.schedule(Event::KeyFrameExchangeTimeout {
        bssid,
        sta_addr,
        frame: frame.clone(),
        attempt,
    });

    let mut buf = Vec::with_capacity(frame.len());
    frame.as_bytes(false, &mut buf);
    inspect_log!(context.inspect.rsn_events, tx_eapol_frame: InspectBytes(&buf));
    context.mlme_sink.send(MlmeRequest::Eapol(fidl_mlme::EapolRequest {
        src_addr: sta_addr,
        dst_addr: bssid,
        data: buf,
    }));
    resp_timeout_id
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
                    rsc: [0u8; 8],
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
                    rsc: [0u8; 8],
                }],
            }));
        }
        _ => error!("derived unexpected key"),
    };
}

fn send_deauthenticate_request(current_bss: Box<BssDescription>, mlme_sink: &MlmeSink) {
    mlme_sink.send(MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
        peer_sta_address: current_bss.bssid.clone(),
        reason_code: fidl_mlme::ReasonCode::StaLeaving,
    }));
}

fn to_associating_state(cmd: ConnectCommand, mlme_sink: &MlmeSink) -> State {
    let s_rsne_data = match &cmd.protection {
        Protection::Open => None,
        Protection::Rsna(rsna) => {
            let s_rsne = rsna.negotiated_rsne.to_full_rsne();
            let mut buf = Vec::with_capacity(s_rsne.len());
            s_rsne.as_bytes(&mut buf);
            Some(buf)
        }
    };

    mlme_sink.send(MlmeRequest::Associate(fidl_mlme::AssociateRequest {
        peer_sta_address: cmd.bss.bssid.clone(),
        rsn: s_rsne_data,
    }));
    State::Associating { cmd }
}

fn handle_supplicant_start_failure(
    responder: Option<Responder<ConnectResult>>,
    bss: Box<BssDescription>,
    context: &Context,
    e: failure::Error,
) {
    error!("deauthenticating; could not start Supplicant: {}", e);
    send_deauthenticate_request(bss, &context.mlme_sink);

    // TODO(hahnr): Report RSNA specific failure instead.
    let reason = fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified;
    report_connect_finished(
        responder,
        &context,
        ConnectResult::Failed,
        Some(ConnectFailure::AssociationFailure(reason)),
    );
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::format_err;
    use fuchsia_inspect as finspect;
    use futures::channel::{mpsc, oneshot};
    use std::error::Error;
    use std::sync::Arc;
    use wlan_common::{ie::rsn::rsne::RsnCapabilities, RadioConfig};
    use wlan_rsn::{rsna::UpdateSink, NegotiatedRsne};

    use crate::client::test_utils::{
        expect_info_event, fake_protected_bss_description, fake_unprotected_bss_description,
        mock_supplicant, MockSupplicant, MockSupplicantController,
    };
    use crate::client::{inspect, InfoSink, TimeStream};
    use crate::{test_utils, timer, DeviceInfo, InfoStream, MlmeStream, Ssid};

    #[test]
    fn associate_happy_path_unprotected() {
        let mut h = TestHelper::new();

        let state = idle_state();
        let (command, receiver) = connect_command_one();
        let bss_ssid = command.bss.ssid.clone();
        let bssid = command.bss.bssid.clone();

        // Issue a "connect" command
        let state = state.connect(command, &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::AssociationStarted { att_id: 1 });
        expect_join_request(&mut h.mlme_stream, &bss_ssid);

        // (mlme->sme) Send a JoinConf as a response
        let join_conf = create_join_conf(fidl_mlme::JoinResultCodes::Success);
        let state = state.on_mlme_event(join_conf, &mut h.context);

        expect_auth_req(&mut h.mlme_stream, bssid);

        // (mlme->sme) Send an AuthenticateConf as a response
        let auth_conf =
            create_auth_conf(bssid.clone(), fidl_mlme::AuthenticateResultCodes::Success);
        let state = state.on_mlme_event(auth_conf, &mut h.context);

        expect_assoc_req(&mut h.mlme_stream, bssid);

        // (mlme->sme) Send an AssociateConf
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let _state = state.on_mlme_event(assoc_conf, &mut h.context);

        // User should be notified that we are connected
        expect_result(receiver, ConnectResult::Success);

        expect_info_event(&mut h.info_stream, InfoEvent::AssociationSuccess { att_id: 1 });
        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished { result: ConnectResult::Success, failure: None },
        );
    }

    #[test]
    fn associate_happy_path_protected() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();

        let state = idle_state();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bss_ssid = command.bss.ssid.clone();
        let bssid = command.bss.bssid.clone();

        // Issue a "connect" command
        let state = state.connect(command, &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::AssociationStarted { att_id: 1 });
        expect_join_request(&mut h.mlme_stream, &bss_ssid);

        // (mlme->sme) Send a JoinConf as a response
        let join_conf = create_join_conf(fidl_mlme::JoinResultCodes::Success);
        let state = state.on_mlme_event(join_conf, &mut h.context);

        expect_auth_req(&mut h.mlme_stream, bssid);

        // (mlme->sme) Send an AuthenticateConf as a response
        let auth_conf =
            create_auth_conf(bssid.clone(), fidl_mlme::AuthenticateResultCodes::Success);
        let state = state.on_mlme_event(auth_conf, &mut h.context);

        expect_assoc_req(&mut h.mlme_stream, bssid);

        // (mlme->sme) Send an AssociateConf
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);

        assert!(suppl_mock.is_supplicant_started());
        expect_info_event(&mut h.info_stream, InfoEvent::AssociationSuccess { att_id: 1 });
        expect_info_event(&mut h.info_stream, InfoEvent::RsnaStarted { att_id: 1 });

        // (mlme->sme) Send an EapolInd, mock supplicant with key frame
        let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
        let state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![update]);

        expect_eapol_req(&mut h.mlme_stream, bssid);

        // (mlme->sme) Send an EapolInd, mock supplicant with keys
        let ptk = SecAssocUpdate::Key(Key::Ptk(test_utils::ptk()));
        let gtk = SecAssocUpdate::Key(Key::Gtk(test_utils::gtk()));
        let state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![ptk, gtk]);

        expect_set_ptk(&mut h.mlme_stream, bssid);
        expect_set_gtk(&mut h.mlme_stream);

        // (mlme->sme) Send an EapolInd, mock supplicant with completion status
        let update = SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished);
        let _state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![update]);

        expect_set_ctrl_port(&mut h.mlme_stream, bssid, fidl_mlme::ControlledPortState::Open);
        expect_result(receiver, ConnectResult::Success);
        expect_info_event(&mut h.info_stream, InfoEvent::RsnaEstablished { att_id: 1 });
        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished { result: ConnectResult::Success, failure: None },
        );
    }

    #[test]
    fn join_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();
        // Start in a "Joining" state
        let state = State::Joining { cmd };

        // (mlme->sme) Send an unsuccessful JoinConf
        let join_conf = MlmeEvent::JoinConf {
            resp: fidl_mlme::JoinConfirm {
                result_code: fidl_mlme::JoinResultCodes::JoinFailureTimeout,
            },
        };
        let state = state.on_mlme_event(join_conf, &mut h.context);
        assert_idle(state);

        // User should be notified that connection attempt failed
        expect_result(receiver, ConnectResult::Failed);

        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished {
                result: ConnectResult::Failed,
                failure: Some(ConnectFailure::JoinFailure(
                    fidl_mlme::JoinResultCodes::JoinFailureTimeout,
                )),
            },
        );
    }

    #[test]
    fn authenticate_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();

        // Start in an "Authenticating" state
        let state = State::Authenticating { cmd };

        // (mlme->sme) Send an unsuccessful AuthenticateConf
        let auth_conf = MlmeEvent::AuthenticateConf {
            resp: fidl_mlme::AuthenticateConfirm {
                peer_sta_address: connect_command_one().0.bss.bssid,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
            },
        };
        let state = state.on_mlme_event(auth_conf, &mut h.context);
        assert_idle(state);

        // User should be notified that connection attempt failed
        expect_result(receiver, ConnectResult::Failed);

        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished {
                result: ConnectResult::Failed,
                failure: Some(ConnectFailure::AuthenticationFailure(
                    fidl_mlme::AuthenticateResultCodes::Refused,
                )),
            },
        );
    }

    #[test]
    fn associate_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();

        // Start in an "Associating" state
        let state = State::Associating { cmd };

        // (mlme->sme) Send an unsuccessful AssociateConf
        let assoc_conf =
            create_assoc_conf(fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);
        assert_idle(state);

        // User should be notified that connection attempt failed
        expect_result(receiver, ConnectResult::Failed);

        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished {
                result: ConnectResult::Failed,
                failure: Some(ConnectFailure::AssociationFailure(
                    fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                )),
            },
        );
    }

    #[test]
    fn connect_while_joining() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = joining_state(cmd_one);
        let (cmd_two, _receiver_two) = connect_command_two();
        let state = state.connect(cmd_two, &mut h.context);
        expect_result(receiver_one, ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream, &connect_command_two().0.bss.ssid);
        assert_joining(state, &connect_command_two().0.bss);
    }

    #[test]
    fn connect_while_authenticating() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = authenticating_state(cmd_one);
        let (cmd_two, _receiver_two) = connect_command_two();
        let state = state.connect(cmd_two, &mut h.context);
        expect_result(receiver_one, ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream, &connect_command_two().0.bss.ssid);
        assert_joining(state, &connect_command_two().0.bss);
    }

    #[test]
    fn connect_while_associating() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = associating_state(cmd_one);
        let (cmd_two, _receiver_two) = connect_command_two();
        let state = state.connect(cmd_two, &mut h.context);
        let state = exchange_deauth(state, &mut h);
        expect_result(receiver_one, ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream, &connect_command_two().0.bss.ssid);
        assert_joining(state, &connect_command_two().0.bss);
    }

    #[test]
    fn supplicant_fails_to_start_while_associating() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();
        let state = associating_state(command);

        suppl_mock.set_start_failure(format_err!("failed to start supplicant"));

        // (mlme->sme) Send an AssociateConf
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let _state = state.on_mlme_event(assoc_conf, &mut h.context);

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, ConnectResult::Failed);
        expect_info_event(&mut h.info_stream, InfoEvent::AssociationSuccess { att_id: 0 });
        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished {
                result: ConnectResult::Failed,
                failure: Some(ConnectFailure::AssociationFailure(
                    fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                )),
            },
        );
    }

    #[test]
    fn bad_eapol_frame_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let (command, mut receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();
        let state = establishing_rsna_state(command);

        // doesn't matter what we mock here
        let update = SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished);
        suppl_mock.set_on_eapol_frame_results(vec![update]);

        // (mlme->sme) Send an EapolInd with bad eapol data
        let eapol_ind = create_eapol_ind(bssid.clone(), vec![1, 2, 3, 4]);
        let state = state.on_mlme_event(eapol_ind, &mut h.context);

        assert_eq!(Ok(None), receiver.try_recv());

        match state {
            State::Associated { link_state, .. } => match link_state {
                LinkState::EstablishingRsna { .. } => (), // expected path
                _ => panic!("expect link state to still be establishing RSNA"),
            },
            _ => panic!("expect state to still be associated"),
        }

        expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");
        expect_stream_empty(&mut h.info_stream, "unexpected event in info stream");
    }

    #[test]
    fn supplicant_fails_to_process_eapol_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let (command, mut receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();
        let state = establishing_rsna_state(command);

        suppl_mock.set_on_eapol_frame_failure(format_err!("supplicant::on_eapol_frame fails"));

        // (mlme->sme) Send an EapolInd
        let eapol_ind = create_eapol_ind(bssid.clone(), test_utils::eapol_key_frame_bytes());
        let state = state.on_mlme_event(eapol_ind, &mut h.context);

        assert_eq!(Ok(None), receiver.try_recv());

        match state {
            State::Associated { link_state, .. } => match link_state {
                LinkState::EstablishingRsna { .. } => (), // expected path
                _ => panic!("expect link state to still be establishing RSNA"),
            },
            _ => panic!("expect state to still be associated"),
        }

        expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");
        expect_stream_empty(&mut h.info_stream, "unexpected event in info stream");
    }

    #[test]
    fn reject_foreign_eapol_frames() {
        let mut h = TestHelper::new();
        let (supplicant, mock) = mock_supplicant();
        let state = link_up_state_protected(supplicant, [7; 6]);
        mock.set_on_eapol_frame_callback(|| {
            panic!("eapol frame should not have been processed");
        });

        // Send an EapolInd from foreign BSS.
        let eapol_ind = create_eapol_ind([1; 6], test_utils::eapol_key_frame_bytes());
        let state = state.on_mlme_event(eapol_ind, &mut h.context);

        // Verify state did not change.
        match state {
            State::Associated { link_state, .. } => match link_state {
                LinkState::LinkUp { protection: Protection::Rsna(_) } => (), // expected path
                _ => panic!("expected unchanged link state"),
            },
            _ => panic!("not associated anymore"),
        }
    }

    #[test]
    fn wrong_password_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();
        let state = establishing_rsna_state(command);

        // (mlme->sme) Send an EapolInd, mock supplicant with wrong password status
        let update = SecAssocUpdate::Status(SecAssocStatus::WrongPassword);
        let _state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![update]);

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, ConnectResult::BadCredentials);
        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished { result: ConnectResult::BadCredentials, failure: None },
        );
    }

    #[test]
    fn overall_timeout_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, _suppl_mock) = mock_supplicant();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();

        // Start in an "Associating" state
        let state = State::Associating { cmd: command };
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);

        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        match timed_event.event {
            Event::EstablishingRsnaTimeout => (), // expected path
            _ => panic!("expect EstablishingRsnaTimeout timeout event"),
        }

        expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");

        let _state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, ConnectResult::Failed);
    }

    #[test]
    fn key_frame_exchange_timeout_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();
        let state = establishing_rsna_state(command);

        // (mlme->sme) Send an EapolInd, mock supplication with key frame
        let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
        let mut state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![update]);

        for i in 1..=3 {
            println!("send eapol attempt: {}", i);
            expect_eapol_req(&mut h.mlme_stream, bssid);
            expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");

            let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
            match timed_event.event {
                Event::KeyFrameExchangeTimeout { attempt, .. } => assert_eq!(attempt, i),
                _ => panic!("expect EstablishingRsnaTimeout timeout event"),
            }
            state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);
        }

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, ConnectResult::Failed);
    }

    #[test]
    fn gtk_rotation_during_link_up() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();
        let bssid = [7; 6];
        let state = link_up_state_protected(supplicant, bssid);

        // (mlme->sme) Send an EapolInd, mock supplication with key frame and GTK
        let key_frame = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
        let gtk = SecAssocUpdate::Key(Key::Gtk(test_utils::gtk()));
        let mut state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![key_frame, gtk]);

        // EAPoL frame is sent out, but state still remains the same
        expect_eapol_req(&mut h.mlme_stream, bssid);
        expect_set_gtk(&mut h.mlme_stream);
        expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");
        match state {
            State::Associated { link_state: LinkState::LinkUp { .. }, .. } => (), // expected
            _ => panic!("expect still in link-up state"),
        }

        // Any timeout is ignored
        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);
        match state {
            State::Associated { link_state: LinkState::LinkUp { .. }, .. } => (), // expected
            _ => panic!("expect still in link-up state"),
        }
    }

    #[test]
    fn connect_while_link_up() {
        let mut h = TestHelper::new();
        let state = link_up_state(connect_command_one().0.bss);
        let state = state.connect(connect_command_two().0, &mut h.context);
        let state = exchange_deauth(state, &mut h);
        expect_join_request(&mut h.mlme_stream, &connect_command_two().0.bss.ssid);
        assert_joining(state, &connect_command_two().0.bss);
    }

    #[test]
    fn disconnect_while_idle() {
        let mut h = TestHelper::new();
        let new_state = idle_state().disconnect(&mut h.context);
        assert_idle(new_state);
        // Expect no messages to the MLME
        assert!(h.mlme_stream.try_next().is_err());
    }

    #[test]
    fn disconnect_while_joining() {
        let mut h = TestHelper::new();
        let (cmd, receiver) = connect_command_one();
        let state = joining_state(cmd);
        let state = state.disconnect(&mut h.context);
        expect_result(receiver, ConnectResult::Canceled);
        assert_idle(state);
    }

    #[test]
    fn disconnect_while_authenticating() {
        let mut h = TestHelper::new();
        let (cmd, receiver) = connect_command_one();
        let state = authenticating_state(cmd);
        let state = state.disconnect(&mut h.context);
        expect_result(receiver, ConnectResult::Canceled);
        assert_idle(state);
    }

    #[test]
    fn disconnect_while_associating() {
        let mut h = TestHelper::new();
        let (cmd, receiver) = connect_command_one();
        let state = associating_state(cmd);
        let state = state.disconnect(&mut h.context);
        let state = exchange_deauth(state, &mut h);
        expect_result(receiver, ConnectResult::Canceled);
        assert_idle(state);
    }

    #[test]
    fn disconnect_while_link_up() {
        let mut h = TestHelper::new();
        let state = link_up_state(connect_command_one().0.bss);
        let state = state.disconnect(&mut h.context);
        let state = exchange_deauth(state, &mut h);
        assert_idle(state);
    }

    #[test]
    fn increment_att_id_on_connect() {
        let mut h = TestHelper::new();
        let state = idle_state();
        assert_eq!(h.context.att_id, 0);

        let state = state.connect(connect_command_one().0, &mut h.context);
        assert_eq!(h.context.att_id, 1);

        let state = state.disconnect(&mut h.context);
        assert_eq!(h.context.att_id, 1);

        let state = state.connect(connect_command_two().0, &mut h.context);
        assert_eq!(h.context.att_id, 2);

        let _state = state.connect(connect_command_one().0, &mut h.context);
        assert_eq!(h.context.att_id, 3);
    }

    #[test]
    fn increment_att_id_on_disassociate_ind() {
        let mut h = TestHelper::new();
        let state = link_up_state(Box::new(unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])));
        assert_eq!(h.context.att_id, 0);

        let disassociate_ind = MlmeEvent::DisassociateInd {
            ind: fidl_mlme::DisassociateIndication {
                peer_sta_address: [0, 0, 0, 0, 0, 0],
                reason_code: 0,
            },
        };

        let state = state.on_mlme_event(disassociate_ind, &mut h.context);
        assert_associating(state, &unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8]));
        assert_eq!(h.context.att_id, 1);
    }

    struct TestHelper {
        mlme_stream: MlmeStream,
        info_stream: InfoStream,
        time_stream: TimeStream,
        context: Context,
    }

    impl TestHelper {
        fn new() -> Self {
            let (mlme_sink, mlme_stream) = mpsc::unbounded();
            let (info_sink, info_stream) = mpsc::unbounded();
            let (timer, time_stream) = timer::create_timer();
            let context = Context {
                device_info: Arc::new(fake_device_info()),
                mlme_sink: MlmeSink::new(mlme_sink),
                info_sink: InfoSink::new(info_sink),
                timer,
                att_id: 0,
                inspect: inspect::SmeNode::new(finspect::ObjectTreeNode::new_root()),
            };
            TestHelper { mlme_stream, info_stream, time_stream, context }
        }
    }

    fn on_eapol_ind(
        state: State,
        helper: &mut TestHelper,
        bssid: [u8; 6],
        suppl_mock: &MockSupplicantController,
        update_sink: UpdateSink,
    ) -> State {
        suppl_mock.set_on_eapol_frame_results(update_sink);
        // (mlme->sme) Send an EapolInd
        let eapol_ind = create_eapol_ind(bssid.clone(), test_utils::eapol_key_frame_bytes());
        state.on_mlme_event(eapol_ind, &mut helper.context)
    }

    fn create_join_conf(result_code: fidl_mlme::JoinResultCodes) -> MlmeEvent {
        MlmeEvent::JoinConf { resp: fidl_mlme::JoinConfirm { result_code } }
    }

    fn create_auth_conf(
        bssid: [u8; 6],
        result_code: fidl_mlme::AuthenticateResultCodes,
    ) -> MlmeEvent {
        MlmeEvent::AuthenticateConf {
            resp: fidl_mlme::AuthenticateConfirm {
                peer_sta_address: bssid,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code,
            },
        }
    }

    fn create_assoc_conf(result_code: fidl_mlme::AssociateResultCodes) -> MlmeEvent {
        MlmeEvent::AssociateConf {
            resp: fidl_mlme::AssociateConfirm { result_code, association_id: 55 },
        }
    }

    fn create_eapol_ind(bssid: [u8; 6], data: Vec<u8>) -> MlmeEvent {
        MlmeEvent::EapolInd {
            ind: fidl_mlme::EapolIndication {
                src_addr: bssid,
                dst_addr: fake_device_info().addr,
                data,
            },
        }
    }

    fn exchange_deauth(state: State, h: &mut TestHelper) -> State {
        // (sme->mlme) Expect a DeauthenticateRequest
        match h.mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Deauthenticate(req)) => {
                assert_eq!(connect_command_one().0.bss.bssid, req.peer_sta_address);
            }
            other => panic!("expected a Deauthenticate request, got {:?}", other),
        }

        // (mlme->sme) Send a DeauthenticateConf as a response
        let deauth_conf = MlmeEvent::DeauthenticateConf {
            resp: fidl_mlme::DeauthenticateConfirm {
                peer_sta_address: connect_command_one().0.bss.bssid,
            },
        };
        state.on_mlme_event(deauth_conf, &mut h.context)
    }

    fn expect_join_request(mlme_stream: &mut MlmeStream, ssid: &[u8]) {
        // (sme->mlme) Expect a JoinRequest
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Join(req)) => assert_eq!(ssid, &req.selected_bss.ssid[..]),
            other => panic!("expect join req to MLME, got {:?}", other),
        }
    }

    fn expect_set_ctrl_port(
        mlme_stream: &mut MlmeStream,
        bssid: [u8; 6],
        state: fidl_mlme::ControlledPortState,
    ) {
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetCtrlPort(req) => {
                assert_eq!(req.peer_sta_address, bssid);
                assert_eq!(req.state, state);
            }
            other => panic!("expected a Join request, got {:?}", other),
        }
    }

    fn expect_auth_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        // (sme->mlme) Expect an AuthenticateRequest
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Authenticate(req)) => assert_eq!(bssid, req.peer_sta_address),
            other => panic!("expected an Authenticate request, got {:?}", other),
        }
    }

    fn expect_deauth_req(
        mlme_stream: &mut MlmeStream,
        bssid: [u8; 6],
        reason_code: fidl_mlme::ReasonCode,
    ) {
        // (sme->mlme) Expect a DeauthenticateRequest
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Deauthenticate(req)) => {
                assert_eq!(bssid, req.peer_sta_address);
                assert_eq!(reason_code, req.reason_code);
            }
            other => panic!("expected an Deauthenticate request, got {:?}", other),
        }
    }

    fn expect_assoc_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Associate(req)) => assert_eq!(bssid, req.peer_sta_address),
            other => panic!("expected an Associate request, got {:?}", other),
        }
    }

    fn expect_eapol_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Eapol(req)) => {
                assert_eq!(req.src_addr, fake_device_info().addr);
                assert_eq!(req.dst_addr, bssid);
                assert_eq!(req.data, test_utils::eapol_key_frame_bytes());
            }
            other => panic!("expected an Eapol request, got {:?}", other),
        }
    }

    fn expect_set_ptk(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetKeys(set_keys_req) => {
                assert_eq!(set_keys_req.keylist.len(), 1);
                let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
                assert_eq!(k.key, vec![0xCCu8; test_utils::cipher().tk_bytes().unwrap()]);
                assert_eq!(k.key_id, 0);
                assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
                assert_eq!(k.address, bssid);
                assert_eq!(k.rsc, [0u8; 8]);
                assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
                assert_eq!(k.cipher_suite_type, 4);
            }
            _ => panic!("expect set keys req to MLME"),
        }
    }

    fn expect_set_gtk(mlme_stream: &mut MlmeStream) {
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetKeys(set_keys_req) => {
                assert_eq!(set_keys_req.keylist.len(), 1);
                let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
                assert_eq!(k.key, test_utils::gtk_bytes());
                assert_eq!(k.key_id, 2);
                assert_eq!(k.key_type, fidl_mlme::KeyType::Group);
                assert_eq!(k.address, [0xFFu8; 6]);
                assert_eq!(k.rsc, [0u8; 8]);
                assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
                assert_eq!(k.cipher_suite_type, 4);
            }
            _ => panic!("expect set keys req to MLME"),
        }
    }

    fn expect_result<T>(mut receiver: oneshot::Receiver<T>, expected_result: T)
    where
        T: PartialEq + ::std::fmt::Debug,
    {
        assert_eq!(Ok(Some(expected_result)), receiver.try_recv());
    }

    fn expect_stream_empty<T>(stream: &mut mpsc::UnboundedReceiver<T>, error_msg: &str) {
        match stream.try_next() {
            Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
            _ => panic!("{}", error_msg),
        }
    }

    fn connect_command_one() -> (ConnectCommand, oneshot::Receiver<ConnectResult>) {
        let (responder, receiver) = Responder::new();
        let cmd = ConnectCommand {
            bss: Box::new(unprotected_bss(b"foo".to_vec(), [7, 7, 7, 7, 7, 7])),
            responder: Some(responder),
            protection: Protection::Open,
            radio_cfg: RadioConfig::default(),
        };
        (cmd, receiver)
    }

    fn connect_command_two() -> (ConnectCommand, oneshot::Receiver<ConnectResult>) {
        let (responder, receiver) = Responder::new();
        let cmd = ConnectCommand {
            bss: Box::new(unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])),
            responder: Some(responder),
            protection: Protection::Open,
            radio_cfg: RadioConfig::default(),
        };
        (cmd, receiver)
    }

    fn connect_command_rsna(
        supplicant: MockSupplicant,
    ) -> (ConnectCommand, oneshot::Receiver<ConnectResult>) {
        let (responder, receiver) = Responder::new();
        let bss = protected_bss(b"foo".to_vec(), [7, 7, 7, 7, 7, 7]);
        let rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
        let cmd = ConnectCommand {
            bss: Box::new(bss),
            responder: Some(responder),
            protection: Protection::Rsna(Rsna {
                negotiated_rsne: NegotiatedRsne::from_rsne(&rsne).expect("invalid NegotiatedRsne"),
                supplicant: Box::new(supplicant),
            }),
            radio_cfg: RadioConfig::default(),
        };
        (cmd, receiver)
    }

    fn idle_state() -> State {
        State::Idle
    }

    fn assert_idle(state: State) {
        match state {
            State::Idle => {}
            other => panic!("Expected an Idle state, got {:?}", other),
        }
    }

    fn joining_state(cmd: ConnectCommand) -> State {
        State::Joining { cmd }
    }

    fn assert_joining(state: State, bss: &BssDescription) {
        match state {
            State::Joining { cmd } => {
                assert_eq!(cmd.bss.as_ref(), bss);
            }
            other => panic!("Expected a Joining state, got {:?}", other),
        }
    }

    fn authenticating_state(cmd: ConnectCommand) -> State {
        State::Authenticating { cmd }
    }

    fn associating_state(cmd: ConnectCommand) -> State {
        State::Associating { cmd }
    }

    fn assert_associating(state: State, bss: &BssDescription) {
        match state {
            State::Associating { cmd } => {
                assert_eq!(cmd.bss.as_ref(), bss);
            }
            other => panic!("Expected an Associating state, got {:?}", other),
        }
    }

    fn establishing_rsna_state(cmd: ConnectCommand) -> State {
        let rsna = match cmd.protection {
            Protection::Rsna(rsna) => rsna,
            _ => panic!("expect rsna for establishing_rsna_state"),
        };
        State::Associated {
            bss: cmd.bss,
            last_rssi: None,
            link_state: LinkState::EstablishingRsna {
                responder: cmd.responder,
                rsna,
                rsna_timeout: None,
                resp_timeout: None,
            },
            radio_cfg: RadioConfig::default(),
        }
    }

    fn link_up_state(bss: Box<fidl_mlme::BssDescription>) -> State {
        State::Associated {
            bss,
            last_rssi: None,
            link_state: LinkState::LinkUp { protection: Protection::Open },
            radio_cfg: RadioConfig::default(),
        }
    }

    fn link_up_state_protected(supplicant: MockSupplicant, bssid: [u8; 6]) -> State {
        let bss = protected_bss(b"foo".to_vec(), bssid);
        let rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
        let rsna = Rsna {
            negotiated_rsne: NegotiatedRsne::from_rsne(&rsne).expect("invalid NegotiatedRsne"),
            supplicant: Box::new(supplicant),
        };
        State::Associated {
            bss: Box::new(bss),
            last_rssi: None,
            link_state: LinkState::LinkUp { protection: Protection::Rsna(rsna) },
            radio_cfg: RadioConfig::default(),
        }
    }

    fn protected_bss(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
        fidl_mlme::BssDescription { bssid, ..fake_protected_bss_description(ssid) }
    }

    fn unprotected_bss(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
        fidl_mlme::BssDescription { bssid, ..fake_unprotected_bss_description(ssid) }
    }

    fn fake_device_info() -> DeviceInfo {
        test_utils::fake_device_info([0, 1, 2, 3, 4, 5])
    }
}
