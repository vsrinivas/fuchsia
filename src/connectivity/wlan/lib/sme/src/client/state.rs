// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription, ChannelSwitchInfo, MlmeEvent};
use fuchsia_inspect_contrib::{inspect_log, log::InspectBytes};
use fuchsia_zircon as zx;
use log::{error, warn};
use wep_deprecated;
use wlan_common::{
    bss::BssDescriptionExt, format::MacFmt, ie::rsn::cipher, ie::write_wpa1_ie, RadioConfig,
};
use wlan_rsn::key::exchange::Key;
use wlan_rsn::rsna::{self, SecAssocStatus, SecAssocUpdate};
use wlan_rsn::ProtectionInfo;

use super::bss::ClientConfig;
use super::rsn::Rsna;
use super::{ConnectFailure, ConnectResult, EstablishRsnaFailure, Status};

use crate::client::{
    event::{self, Event},
    info::ConnectionPingInfo,
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
        since: zx::Time,
        ping_event: Option<EventId>,
    },
}

#[derive(Debug)]
pub enum Protection {
    Open,
    Wep(wep_deprecated::Key),
    // WPA1 is based off of a modified pre-release version of IEEE 802.11i. It is similar enough
    // that we can reuse the existing RSNA implementation rather than duplicating large pieces of
    // logic.
    LegacyWpa(Rsna),
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
    Failed(EstablishRsnaFailure),
    Unchanged,
    Progressed { new_resp_timeout: Option<EventId> },
}

#[derive(Debug)]
pub enum State {
    Idle {
        cfg: ClientConfig,
    },
    Joining {
        cfg: ClientConfig,
        cmd: ConnectCommand,
    },
    Authenticating {
        cfg: ClientConfig,
        cmd: ConnectCommand,
    },
    Associating {
        cfg: ClientConfig,
        cmd: ConnectCommand,
    },
    Associated {
        cfg: ClientConfig,
        bss: Box<BssDescription>,
        last_rssi: i8,
        link_state: LinkState,
        radio_cfg: RadioConfig,
    },
}

impl State {
    fn state_name(&self) -> &'static str {
        match self {
            State::Idle { .. } => IDLE_STATE,
            State::Joining { .. } => JOINING_STATE,
            State::Authenticating { .. } => AUTHENTICATING_STATE,
            State::Associating { .. } => ASSOCIATING_STATE,
            State::Associated { link_state, .. } => match link_state {
                LinkState::EstablishingRsna { .. } => RSNA_STATE,
                LinkState::LinkUp { .. } => LINK_UP_STATE,
            },
        }
    }

    fn cfg(&self) -> &ClientConfig {
        match self {
            State::Idle { cfg }
            | State::Joining { cfg, .. }
            | State::Authenticating { cfg, .. }
            | State::Associating { cfg, .. }
            | State::Associated { cfg, .. } => cfg,
        }
    }

    pub fn on_mlme_event(self, event: MlmeEvent, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_msg: Option<String> = None;

        let new_state = match self {
            State::Idle { .. } => {
                warn!("Unexpected MLME message while Idle: {:?}", event);
                self
            }
            State::Joining { cfg, cmd } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        context.info.report_auth_started();
                        if let Protection::Wep(ref key) = cmd.protection {
                            install_wep_key(context, cmd.bss.bssid.clone(), key);
                            context.mlme_sink.send(MlmeRequest::Authenticate(
                                wep_deprecated::make_mlme_authenticate_request(
                                    cmd.bss.bssid.clone(),
                                    DEFAULT_AUTH_FAILURE_TIMEOUT,
                                ),
                            ));
                        } else {
                            context.mlme_sink.send(MlmeRequest::Authenticate(
                                fidl_mlme::AuthenticateRequest {
                                    peer_sta_address: cmd.bss.bssid.clone(),
                                    auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                    auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                                    auth_content: None,
                                },
                            ));
                        }
                        state_change_msg.replace("successful join".to_string());
                        State::Authenticating { cfg, cmd }
                    }
                    other => {
                        error!("Join request failed with result code {:?}", other);
                        report_connect_finished(
                            cmd.responder,
                            context,
                            ConnectResult::Failed(ConnectFailure::JoinFailure(other)),
                        );
                        state_change_msg.replace(format!("failed join; result code: {:?}", other));
                        State::Idle { cfg }
                    }
                },
                _ => State::Joining { cfg, cmd },
            },
            State::Authenticating { cfg, cmd } => match event {
                MlmeEvent::AuthenticateConf { resp } => match resp.result_code {
                    fidl_mlme::AuthenticateResultCodes::Success => {
                        context.info.report_assoc_started();
                        state_change_msg.replace("successful auth".to_string());
                        to_associating_state(cfg, cmd, &context.mlme_sink)
                    }
                    other => {
                        error!("Authenticate request failed with result code {:?}", other);
                        report_connect_finished(
                            cmd.responder,
                            context,
                            ConnectResult::Failed(ConnectFailure::AuthenticationFailure(other)),
                        );
                        state_change_msg.replace(format!("failed auth; result code: {:?}", other));
                        State::Idle { cfg }
                    }
                },
                MlmeEvent::DeauthenticateInd { ind } => {
                    error!(
                        "authentication request failed due to spurious deauthentication: {:?}",
                        ind.reason_code
                    );
                    report_connect_finished(
                        cmd.responder,
                        context,
                        ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
                            fidl_mlme::AuthenticateResultCodes::Refused,
                        )),
                    );
                    state_change_msg.replace(format!(
                        "received DeauthenticateInd msg while authenticating; reason code {:?}",
                        ind.reason_code
                    ));
                    State::Idle { cfg }
                }
                _ => State::Authenticating { cfg, cmd },
            },
            State::Associating { cfg, cmd } => match event {
                MlmeEvent::AssociateConf { resp } => {
                    handle_mlme_assoc_conf(cfg, resp, cmd, context, &mut state_change_msg)
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    error!(
                        "association request failed due to spurious deauthentication: {:?}",
                        ind.reason_code
                    );
                    report_connect_finished(
                        cmd.responder,
                        context,
                        ConnectResult::Failed(ConnectFailure::AssociationFailure(
                            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                        )),
                    );
                    state_change_msg.replace(format!(
                        "received DeauthenticateInd msg while associating; reason code {:?}",
                        ind.reason_code
                    ));
                    State::Idle { cfg }
                }
                MlmeEvent::DisassociateInd { ind } => {
                    error!(
                        "association request failed due to spurious disassociation: {:?}",
                        ind.reason_code
                    );
                    report_connect_finished(
                        cmd.responder,
                        context,
                        ConnectResult::Failed(ConnectFailure::AssociationFailure(
                            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                        )),
                    );
                    state_change_msg.replace(format!(
                        "received DisassociateInd msg while associating; reason code {:?}",
                        ind.reason_code
                    ));
                    State::Idle { cfg }
                }
                _ => State::Associating { cfg, cmd },
            },
            State::Associated { cfg, bss, last_rssi, link_state, radio_cfg } => match event {
                MlmeEvent::DisassociateInd { .. } => {
                    let (responder, mut protection) = match link_state {
                        LinkState::LinkUp { protection, since, .. } => {
                            let connected_duration = now() - since;
                            context.info.report_connection_lost(
                                connected_duration,
                                last_rssi,
                                bss.bssid,
                                bss.ssid.clone(),
                            );
                            (None, protection)
                        }
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
                    to_associating_state(cfg, cmd, &context.mlme_sink)
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    match link_state {
                        LinkState::EstablishingRsna { responder, .. } => {
                            let connect_result = EstablishRsnaFailure::InternalError.into();
                            report_connect_finished(responder, context, connect_result);
                        }
                        LinkState::LinkUp { since, .. } => {
                            let connected_duration = now() - since;
                            context.info.report_connection_lost(
                                connected_duration,
                                last_rssi,
                                bss.bssid,
                                bss.ssid,
                            );
                        }
                    }
                    state_change_msg.replace(format!(
                        "received DeauthenticateInd msg; reason code {:?}",
                        ind.reason_code
                    ));
                    State::Idle { cfg }
                }
                MlmeEvent::SignalReport { ind } => {
                    State::Associated { cfg, bss, last_rssi: ind.rssi_dbm, link_state, radio_cfg }
                }
                MlmeEvent::EapolInd { ref ind } if bss.needs_eapol_exchange() => {
                    // Reject EAPOL frames from other BSS.
                    if ind.src_addr != bss.bssid {
                        let eapol_pdu = &ind.data[..];
                        inspect_log!(context.inspect.rsn_events.lock(), {
                            rx_eapol_frame: InspectBytes(&eapol_pdu),
                            foreign_bssid: ind.src_addr.to_mac_str(),
                            current_bssid: bss.bssid.to_mac_str(),
                            status: "rejected (foreign BSS)",
                        });
                        return State::Associated { cfg, bss, last_rssi, link_state, radio_cfg };
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
                                context.info.report_rsna_established(context.att_id);
                                report_connect_finished(responder, context, ConnectResult::Success);

                                let now = now();
                                let info = ConnectionPingInfo::first_connected(now);
                                let ping_event = Some(report_ping(info, context));
                                state_change_msg.replace("RSNA established".to_string());
                                let link_state = LinkState::LinkUp {
                                    protection: Protection::Rsna(rsna),
                                    since: now,
                                    ping_event,
                                };
                                State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                            }
                            RsnaStatus::Failed(failure) => {
                                report_connect_finished(responder, context, failure.into());
                                send_deauthenticate_request(bss, &context.mlme_sink);
                                state_change_msg.replace("RSNA failed".to_string());
                                State::Idle { cfg }
                            }
                            RsnaStatus::Unchanged => {
                                let link_state = LinkState::EstablishingRsna {
                                    responder,
                                    rsna,
                                    rsna_timeout,
                                    resp_timeout,
                                };
                                State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
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
                                State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                            }
                        },
                        LinkState::LinkUp { protection, ping_event, since } => {
                            match protection {
                                Protection::Rsna(mut rsna) => {
                                    match process_eapol_ind(context, &mut rsna, &ind) {
                                        RsnaStatus::Unchanged => {}
                                        // This can happen when there's a GTK rotation.
                                        // Timeout is ignored because only one RX frame is needed in
                                        // the exchange, so we are not waiting for another one.
                                        RsnaStatus::Progressed { new_resp_timeout: _ } => {}
                                        // Once re-keying is supported, the RSNA can fail in LinkUp as
                                        // well and cause deauthentication.
                                        s => {
                                            error!("unexpected RsnaStatus in LinkUp state: {:?}", s)
                                        }
                                    };
                                    let link_state = LinkState::LinkUp {
                                        protection: Protection::Rsna(rsna),
                                        ping_event,
                                        since,
                                    };
                                    State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                                }
                                // Drop EAPOL frames if the BSS is not an RSN.
                                _ => {
                                    let link_state =
                                        LinkState::LinkUp { protection, ping_event, since };
                                    State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                                }
                            }
                        }
                    }
                }
                MlmeEvent::OnChannelSwitched { info } => {
                    let bss = process_channel_switch(bss, info);
                    State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                }
                _ => State::Associated { cfg, bss, last_rssi, link_state, radio_cfg },
            },
        };

        if start_state != new_state.state_name() || state_change_msg.is_some() {
            inspect_log!(context.inspect.state_events.lock(), {
                from: start_state,
                to: new_state.state_name(),
                ctx?: state_change_msg,
            });
        }
        new_state
    }

    pub fn handle_timeout(self, event_id: EventId, event: Event, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_msg: Option<String> = None;

        let new_state = match self {
            State::Associated { cfg, bss, last_rssi, link_state, radio_cfg } => match link_state {
                LinkState::EstablishingRsna {
                    responder,
                    rsna,
                    mut rsna_timeout,
                    mut resp_timeout,
                } => match event {
                    Event::EstablishingRsnaTimeout(..) if triggered(&rsna_timeout, event_id) => {
                        error!("timeout establishing RSNA; deauthenticating");
                        cancel(&mut rsna_timeout);
                        report_connect_finished(
                            responder,
                            context,
                            EstablishRsnaFailure::OverallTimeout.into(),
                        );
                        send_deauthenticate_request(bss, &context.mlme_sink);
                        state_change_msg.replace("RSNA timeout".to_string());
                        State::Idle { cfg }
                    }
                    Event::KeyFrameExchangeTimeout(event::KeyFrameExchangeTimeout {
                        bssid,
                        sta_addr,
                        frame,
                        attempt,
                    }) => {
                        if !triggered(&resp_timeout, event_id) {
                            let link_state = LinkState::EstablishingRsna {
                                responder,
                                rsna,
                                rsna_timeout,
                                resp_timeout,
                            };
                            return State::Associated {
                                cfg,
                                bss,
                                last_rssi,
                                link_state,
                                radio_cfg,
                            };
                        }
                        context.info.report_key_exchange_timeout();

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
                            State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                        } else {
                            error!("timeout waiting for key frame for last attempt; deauth");
                            cancel(&mut resp_timeout);
                            report_connect_finished(
                                responder,
                                context,
                                EstablishRsnaFailure::KeyFrameExchangeTimeout.into(),
                            );
                            send_deauthenticate_request(bss, &context.mlme_sink);
                            state_change_msg.replace("key frame rx timeout".to_string());
                            State::Idle { cfg }
                        }
                    }
                    _ => {
                        let link_state = LinkState::EstablishingRsna {
                            responder,
                            rsna,
                            rsna_timeout,
                            resp_timeout,
                        };
                        State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                    }
                },
                LinkState::LinkUp { protection, since, mut ping_event } => match event {
                    Event::ConnectionPing(prev_ping) => {
                        if !triggered(&ping_event, event_id) {
                            let link_state = LinkState::LinkUp { protection, since, ping_event };
                            return State::Associated {
                                cfg,
                                bss,
                                last_rssi,
                                link_state,
                                radio_cfg,
                            };
                        }
                        cancel(&mut ping_event);
                        ping_event.replace(report_ping(prev_ping.next_ping(now()), context));
                        let link_state = LinkState::LinkUp { protection, since, ping_event };
                        State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                    }
                    _ => {
                        let link_state = LinkState::LinkUp { protection, since, ping_event };
                        State::Associated { cfg, bss, last_rssi, link_state, radio_cfg }
                    }
                },
            },
            _ => self,
        };

        if start_state != new_state.state_name() || state_change_msg.is_some() {
            inspect_log!(context.inspect.state_events.lock(), {
                from: start_state,
                to: new_state.state_name(),
                ctx?: state_change_msg,
            });
        }
        new_state
    }

    pub fn connect(self, cmd: ConnectCommand, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let cfg = self.disconnect_internal(context);

        let mut selected_bss = clone_bss_desc(&cmd.bss);
        let (phy_to_use, cbw_to_use) =
            derive_phy_cbw(&selected_bss, &context.device_info, &cmd.radio_cfg);
        selected_bss.chan.cbw = cbw_to_use;

        context.mlme_sink.send(MlmeRequest::Join(fidl_mlme::JoinRequest {
            selected_bss,
            join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
            nav_sync_delay: 0,
            op_rates: vec![],
            phy: phy_to_use,
            cbw: cbw_to_use,
        }));
        context.att_id += 1;
        context.info.report_join_started(context.att_id);

        let msg = connect_cmd_inspect_summary(&cmd);
        inspect_log!(context.inspect.state_events.lock(), {
            from: start_state,
            to: JOINING_STATE,
            ctx: msg,
        });
        State::Joining { cfg, cmd }
    }

    pub fn disconnect(self, context: &mut Context) -> Self {
        inspect_log!(context.inspect.state_events.lock(), {
            from: self.state_name(),
            to: IDLE_STATE,
            ctx: "disconnect command",
        });
        if let State::Associated { bss, .. } = &self {
            context.info.report_manual_disconnect(bss.ssid.clone());
        }
        State::Idle { cfg: self.disconnect_internal(context) }
    }

    fn disconnect_internal(self, context: &mut Context) -> ClientConfig {
        let cfg = self.cfg().clone();
        match self {
            State::Idle { .. } => {}
            State::Joining { cmd, .. } | State::Authenticating { cmd, .. } => {
                report_connect_finished(cmd.responder, context, ConnectResult::Canceled);
            }
            State::Associating { cmd, .. } => {
                report_connect_finished(cmd.responder, context, ConnectResult::Canceled);
                send_deauthenticate_request(cmd.bss, &context.mlme_sink);
            }
            State::Associated { bss, .. } => {
                send_deauthenticate_request(bss, &context.mlme_sink);
            }
        }
        cfg
    }

    // Cancel any connect that is in progress. No-op if client is already idle or connected.
    pub fn cancel_ongoing_connect(self, context: &mut Context) -> Self {
        // Only move to idle if client is not already connected. Technically, SME being in
        // transition state does not necessarily mean that a (manual) connect attempt is
        // in progress (since DisassociateInd moves SME to transition state). However, the
        // main thing we are concerned about is that we don't disconnect from an already
        // connected state until the new connect attempt succeeds in selecting BSS.
        if self.in_transition_state() {
            State::Idle { cfg: self.disconnect_internal(context) }
        } else {
            self
        }
    }

    fn in_transition_state(&self) -> bool {
        match self {
            State::Idle { .. } | State::Associated { link_state: LinkState::LinkUp { .. }, .. } => {
                false
            }
            _ => true,
        }
    }

    pub fn status(&self) -> Status {
        match self {
            State::Idle { .. } => Status { connected_to: None, connecting_to: None },
            State::Joining { cmd, .. }
            | State::Authenticating { cmd, .. }
            | State::Associating { cmd, .. } => {
                Status { connected_to: None, connecting_to: Some(cmd.bss.ssid.clone()) }
            }
            State::Associated { bss, link_state: LinkState::EstablishingRsna { .. }, .. } => {
                Status { connected_to: None, connecting_to: Some(bss.ssid.clone()) }
            }
            State::Associated { cfg, bss, link_state: LinkState::LinkUp { .. }, .. } => {
                Status { connected_to: Some(cfg.convert_bss_description(bss)), connecting_to: None }
            }
        }
    }
}

fn install_wep_key(context: &mut Context, bssid: [u8; 6], key: &wep_deprecated::Key) {
    let cipher_suite = match key {
        wep_deprecated::Key::Bits40(_) => cipher::WEP_40,
        wep_deprecated::Key::Bits104(_) => cipher::WEP_104,
    };
    // unwrap() is safe, OUI is defined in RSN and always compatible with ciphers.
    let cipher = cipher::Cipher::new_dot11(cipher_suite);
    inspect_log!(context.inspect.rsn_events.lock(), {
        derived_key: "WEP",
        cipher: format!("{:?}", cipher),
        key_index: 0,
    });
    context
        .mlme_sink
        .send(MlmeRequest::SetKeys(wep_deprecated::make_mlme_set_keys_request(bssid, key)));
}

fn handle_mlme_assoc_conf(
    cfg: ClientConfig,
    resp: fidl_mlme::AssociateConfirm,
    cmd: ConnectCommand,
    context: &mut Context,
    state_change_msg: &mut Option<String>,
) -> State {
    match resp.result_code {
        fidl_mlme::AssociateResultCodes::Success => {
            context.info.report_assoc_success(context.att_id);
            match cmd.protection {
                Protection::Rsna(mut rsna) | Protection::LegacyWpa(mut rsna) => {
                    context.info.report_rsna_started(context.att_id);
                    match rsna.supplicant.start() {
                        Err(e) => {
                            handle_supplicant_start_failure(cmd.responder, cmd.bss, context, e);
                            state_change_msg.replace("supplicant failed to start".to_string());
                            State::Idle { cfg }
                        }
                        Ok(_) => {
                            let rsna_timeout =
                                Some(context.timer.schedule(event::EstablishingRsnaTimeout));
                            state_change_msg.replace("successful association".to_string());
                            let last_rssi = cmd.bss.rssi_dbm;
                            State::Associated {
                                cfg,
                                bss: cmd.bss,
                                last_rssi,
                                link_state: LinkState::EstablishingRsna {
                                    responder: cmd.responder,
                                    rsna,
                                    rsna_timeout,
                                    resp_timeout: None,
                                },
                                radio_cfg: cmd.radio_cfg,
                            }
                        }
                    }
                }
                Protection::Open | Protection::Wep(_) => {
                    report_connect_finished(cmd.responder, context, ConnectResult::Success);
                    let now = now();
                    let info = ConnectionPingInfo::first_connected(now);
                    let ping_event = Some(report_ping(info, context));
                    state_change_msg.replace("successful association".to_string());
                    let last_rssi = cmd.bss.rssi_dbm;
                    State::Associated {
                        cfg,
                        bss: cmd.bss,
                        last_rssi,
                        link_state: LinkState::LinkUp {
                            protection: Protection::Open,
                            since: now,
                            ping_event,
                        },
                        radio_cfg: cmd.radio_cfg,
                    }
                }
            }
        }
        other => {
            error!("Associate request failed with result code {:?}", other);
            report_connect_finished(
                cmd.responder,
                context,
                ConnectResult::Failed(ConnectFailure::AssociationFailure(other)),
            );
            state_change_msg.replace(format!("failed association; result code: {:?}", other));
            State::Idle { cfg }
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
         bssid: {bssid}, ssid: {ssid:?}, cap: {cap:?}, rates: {rates:?}, \
         protected: {protected:?}, chan: {chan:?}, \
         rcpi: {rcpi:?}, rsni: {rsni:?}, rssi: {rssi:?}, ht_cap: {ht_cap:?}, ht_op: {ht_op:?}, \
         vht_cap: {vht_cap:?}, vht_op: {vht_op:?} }}",
        bssid = bss.bssid.to_mac_str(),
        ssid = ssid,
        cap = bss.cap,
        rates = bss.rates,
        protected = bss.rsne.is_some(),
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
            context.info.report_supplicant_error(e);
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
        }
    }

    RsnaStatus::Progressed { new_resp_timeout }
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

fn send_deauthenticate_request(current_bss: Box<BssDescription>, mlme_sink: &MlmeSink) {
    mlme_sink.send(MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
        peer_sta_address: current_bss.bssid.clone(),
        reason_code: fidl_mlme::ReasonCode::StaLeaving,
    }));
}

fn to_associating_state(cfg: ClientConfig, cmd: ConnectCommand, mlme_sink: &MlmeSink) -> State {
    match &cmd.protection {
        Protection::Open | Protection::Wep(_) => {
            mlme_sink.send(MlmeRequest::Associate(fidl_mlme::AssociateRequest {
                peer_sta_address: cmd.bss.bssid.clone(),
                rsne: None,
                vendor_ies: None,
            }));
        }
        Protection::LegacyWpa(rsna) => {
            let s_protection = rsna.negotiated_protection.to_full_protection();
            let s_wpa = match s_protection {
                ProtectionInfo::Rsne(_) => {
                    error!("found RSNE protection inside a WPA1 association...");
                    return State::Idle { cfg };
                }
                ProtectionInfo::LegacyWpa(wpa) => wpa,
            };
            let mut buf = vec![];
            write_wpa1_ie(&mut buf, &s_wpa).expect("writing WPA IE into Vec can never fail");
            mlme_sink.send(MlmeRequest::Associate(fidl_mlme::AssociateRequest {
                peer_sta_address: cmd.bss.bssid.clone(),
                rsne: None,
                vendor_ies: Some(buf),
            }));
        }
        Protection::Rsna(rsna) => {
            let s_protection = rsna.negotiated_protection.to_full_protection();
            let s_rsne = match s_protection {
                ProtectionInfo::Rsne(rsne) => rsne,
                ProtectionInfo::LegacyWpa(_) => {
                    error!("found WPA protection inside an RSNA...");
                    return State::Idle { cfg };
                }
            };
            let mut buf = Vec::with_capacity(s_rsne.len());
            // Writing an RSNE into a Vector can never fail as a Vector can be grown when more
            // space is required. If this panic ever triggers, something is clearly broken
            // somewhere else.
            let () = s_rsne.write_into(&mut buf).expect("writing RSNE into Vec can never fail");
            mlme_sink.send(MlmeRequest::Associate(fidl_mlme::AssociateRequest {
                peer_sta_address: cmd.bss.bssid.clone(),
                rsne: Some(buf),
                vendor_ies: None,
            }));
        }
    };
    State::Associating { cfg, cmd }
}

pub fn process_channel_switch(
    mut bss: Box<BssDescription>,
    ind: ChannelSwitchInfo,
) -> Box<BssDescription> {
    // Right now we just update the stored bss description, but we may want to provide some notice
    // or metric for this in the future.
    bss.chan.primary = ind.new_channel;
    bss
}

fn handle_supplicant_start_failure(
    responder: Option<Responder<ConnectResult>>,
    bss: Box<BssDescription>,
    context: &mut Context,
    e: failure::Error,
) {
    error!("deauthenticating; could not start Supplicant: {}", e);
    send_deauthenticate_request(bss, &context.mlme_sink);
    context.info.report_supplicant_error(e);

    report_connect_finished(responder, context, EstablishRsnaFailure::StartSupplicantFailed.into());
}

fn report_ping(info: ConnectionPingInfo, context: &mut Context) -> EventId {
    context.info.report_connection_ping(info.clone());
    context.timer.schedule(info)
}

fn now() -> zx::Time {
    zx::Time::get(zx::ClockId::Monotonic)
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::format_err;
    use fuchsia_inspect::Inspector;
    use futures::channel::{mpsc, oneshot};
    use std::sync::Arc;
    use wlan_common::{assert_variant, ie::rsn::rsne::RsnCapabilities, RadioConfig};
    use wlan_rsn::{rsna::UpdateSink, NegotiatedProtection};

    use crate::client::test_utils::{
        create_assoc_conf, create_auth_conf, create_join_conf, expect_info_event,
        expect_stream_empty, fake_protected_bss_description, fake_unprotected_bss_description,
        fake_wep_bss_description, fake_wpa1_bss_description, mock_supplicant, MockSupplicant,
        MockSupplicantController,
    };
    use crate::client::{info::InfoReporter, inspect, InfoEvent, InfoSink, TimeStream};
    use crate::test_utils::make_wpa1_ie;

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

        expect_info_event(&mut h.info_stream, InfoEvent::JoinStarted { att_id: 1 });
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
            InfoEvent::ConnectFinished { result: ConnectResult::Success },
        );
    }

    #[test]
    fn connect_to_wep_network() {
        let mut h = TestHelper::new();

        let state = idle_state();
        let (command, receiver) = connect_command_wep();
        let bss_ssid = command.bss.ssid.clone();
        let bssid = command.bss.bssid.clone();

        // Issue a "connect" command
        let state = state.connect(command, &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::JoinStarted { att_id: 1 });
        expect_join_request(&mut h.mlme_stream, &bss_ssid);

        // (mlme->sme) Send a JoinConf as a response
        let join_conf = create_join_conf(fidl_mlme::JoinResultCodes::Success);
        let state = state.on_mlme_event(join_conf, &mut h.context);

        // (sme->mlme) Expect an SetKeysRequest
        expect_set_wep_key(&mut h.mlme_stream, bssid, vec![3; 5]);
        // (sme->mlme) Expect an AuthenticateRequest
        assert_variant!(&mut h.mlme_stream.try_next(),
            Ok(Some(MlmeRequest::Authenticate(req))) => {
                assert_eq!(fidl_mlme::AuthenticationTypes::SharedKey, req.auth_type);
                assert_eq!(bssid, req.peer_sta_address);
            }
        );

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
            InfoEvent::ConnectFinished { result: ConnectResult::Success },
        );
    }

    #[test]
    fn connect_to_wpa1_network() {
        let mut h = TestHelper::new();
        let (supplicant, suppl_mock) = mock_supplicant();

        let state = idle_state();
        let (command, receiver) = connect_command_wpa1(supplicant);
        let bss_ssid = command.bss.ssid.clone();
        let bssid = command.bss.bssid.clone();

        // Issue a "connect" command
        let state = state.connect(command, &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::JoinStarted { att_id: 1 });
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
        let ptk = SecAssocUpdate::Key(Key::Ptk(test_utils::wpa1_ptk()));
        let gtk = SecAssocUpdate::Key(Key::Gtk(test_utils::wpa1_gtk()));
        let state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![ptk, gtk]);

        expect_set_wpa1_ptk(&mut h.mlme_stream, bssid);
        expect_set_wpa1_gtk(&mut h.mlme_stream);

        // (mlme->sme) Send an EapolInd, mock supplicant with completion status
        let update = SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished);
        let _state = on_eapol_ind(state, &mut h, bssid, &suppl_mock, vec![update]);

        expect_set_ctrl_port(&mut h.mlme_stream, bssid, fidl_mlme::ControlledPortState::Open);
        expect_result(receiver, ConnectResult::Success);
        expect_info_event(&mut h.info_stream, InfoEvent::RsnaEstablished { att_id: 1 });
        expect_info_event(
            &mut h.info_stream,
            InfoEvent::ConnectFinished { result: ConnectResult::Success },
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

        expect_info_event(&mut h.info_stream, InfoEvent::JoinStarted { att_id: 1 });
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
            InfoEvent::ConnectFinished { result: ConnectResult::Success },
        );
    }

    #[test]
    fn join_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();
        // Start in a "Joining" state
        let state = State::Joining { cfg: ClientConfig::default(), cmd };

        // (mlme->sme) Send an unsuccessful JoinConf
        let join_conf = MlmeEvent::JoinConf {
            resp: fidl_mlme::JoinConfirm {
                result_code: fidl_mlme::JoinResultCodes::JoinFailureTimeout,
            },
        };
        let state = state.on_mlme_event(join_conf, &mut h.context);
        assert_idle(state);

        let result = ConnectResult::Failed(ConnectFailure::JoinFailure(
            fidl_mlme::JoinResultCodes::JoinFailureTimeout,
        ));
        // User should be notified that connection attempt failed
        expect_result(receiver, result.clone());

        expect_info_event(&mut h.info_stream, InfoEvent::ConnectFinished { result });
    }

    #[test]
    fn authenticate_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();

        // Start in an "Authenticating" state
        let state = State::Authenticating { cfg: ClientConfig::default(), cmd };

        // (mlme->sme) Send an unsuccessful AuthenticateConf
        let auth_conf = MlmeEvent::AuthenticateConf {
            resp: fidl_mlme::AuthenticateConfirm {
                peer_sta_address: connect_command_one().0.bss.bssid,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
                auth_content: None,
            },
        };
        let state = state.on_mlme_event(auth_conf, &mut h.context);
        assert_idle(state);

        let result = ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
            fidl_mlme::AuthenticateResultCodes::Refused,
        ));
        // User should be notified that connection attempt failed
        expect_result(receiver, result.clone());

        expect_info_event(&mut h.info_stream, InfoEvent::ConnectFinished { result });
    }

    #[test]
    fn associate_failure() {
        let mut h = TestHelper::new();

        let (cmd, receiver) = connect_command_one();

        // Start in an "Associating" state
        let state = State::Associating { cfg: ClientConfig::default(), cmd };

        // (mlme->sme) Send an unsuccessful AssociateConf
        let assoc_conf =
            create_assoc_conf(fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);
        assert_idle(state);

        let result = ConnectResult::Failed(ConnectFailure::AssociationFailure(
            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
        ));
        // User should be notified that connection attempt failed
        expect_result(receiver, result.clone());

        expect_info_event(&mut h.info_stream, InfoEvent::ConnectFinished { result });
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
    fn deauth_while_authing() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = authenticating_state(cmd_one);
        let deauth_ind = MlmeEvent::DeauthenticateInd {
            ind: fidl_mlme::DeauthenticateIndication {
                peer_sta_address: [7, 7, 7, 7, 7, 7],
                reason_code: fidl_mlme::ReasonCode::UnspecifiedReason,
            },
        };
        let state = state.on_mlme_event(deauth_ind, &mut h.context);
        expect_result(
            receiver_one,
            ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
                fidl_mlme::AuthenticateResultCodes::Refused,
            )),
        );
        assert_idle(state);
    }

    #[test]
    fn deauth_while_associating() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = associating_state(cmd_one);
        let deauth_ind = MlmeEvent::DeauthenticateInd {
            ind: fidl_mlme::DeauthenticateIndication {
                peer_sta_address: [7, 7, 7, 7, 7, 7],
                reason_code: fidl_mlme::ReasonCode::UnspecifiedReason,
            },
        };
        let state = state.on_mlme_event(deauth_ind, &mut h.context);
        expect_result(
            receiver_one,
            ConnectResult::Failed(ConnectFailure::AssociationFailure(
                fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            )),
        );
        assert_idle(state);
    }

    #[test]
    fn disassoc_while_associating() {
        let mut h = TestHelper::new();
        let (cmd_one, receiver_one) = connect_command_one();
        let state = associating_state(cmd_one);
        let disassoc_ind = MlmeEvent::DisassociateInd {
            ind: fidl_mlme::DisassociateIndication {
                peer_sta_address: [7, 7, 7, 7, 7, 7],
                reason_code: 42,
            },
        };
        let state = state.on_mlme_event(disassoc_ind, &mut h.context);
        expect_result(
            receiver_one,
            ConnectResult::Failed(ConnectFailure::AssociationFailure(
                fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            )),
        );
        assert_idle(state);
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
        let result: ConnectResult = EstablishRsnaFailure::StartSupplicantFailed.into();
        expect_result(receiver, result.clone());
        expect_info_event(&mut h.info_stream, InfoEvent::AssociationSuccess { att_id: 0 });
        expect_info_event(&mut h.info_stream, InfoEvent::RsnaStarted { att_id: 0 });
        expect_info_event(&mut h.info_stream, InfoEvent::ConnectFinished { result });
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
        let s = state.on_mlme_event(eapol_ind, &mut h.context);

        assert_eq!(Ok(None), receiver.try_recv());
        assert_variant!(s, State::Associated { link_state: LinkState::EstablishingRsna { .. }, .. });

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
        let eapol_ind = create_eapol_ind(bssid.clone(), test_utils::eapol_key_frame().into());
        let s = state.on_mlme_event(eapol_ind, &mut h.context);

        assert_eq!(Ok(None), receiver.try_recv());
        assert_variant!(s, State::Associated { link_state: LinkState::EstablishingRsna { .. }, .. });

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
        let eapol_ind = create_eapol_ind([1; 6], test_utils::eapol_key_frame().into());
        let state = state.on_mlme_event(eapol_ind, &mut h.context);

        // Verify state did not change.
        assert_variant!(state, State::Associated {
            link_state: LinkState::LinkUp { protection: Protection::Rsna(_), .. },
            ..
        });
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
        let result: ConnectResult = EstablishRsnaFailure::InternalError.into();
        expect_result(receiver, result.clone());
        expect_info_event(&mut h.info_stream, InfoEvent::ConnectFinished { result });
    }

    #[test]
    fn overall_timeout_while_establishing_rsna() {
        let mut h = TestHelper::new();
        let (supplicant, _suppl_mock) = mock_supplicant();
        let (command, receiver) = connect_command_rsna(supplicant);
        let bssid = command.bss.bssid.clone();

        // Start in an "Associating" state
        let state = State::Associating { cfg: ClientConfig::default(), cmd: command };
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);

        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        assert_variant!(timed_event.event, Event::EstablishingRsnaTimeout(..));

        expect_stream_empty(&mut h.mlme_stream, "unexpected event in mlme stream");

        let _state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, EstablishRsnaFailure::OverallTimeout.into());
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
            assert_variant!(timed_event.event, Event::KeyFrameExchangeTimeout(ref event) => {
                assert_eq!(event.attempt, i)
            });
            state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);
        }

        expect_deauth_req(&mut h.mlme_stream, bssid, fidl_mlme::ReasonCode::StaLeaving);
        expect_result(receiver, EstablishRsnaFailure::KeyFrameExchangeTimeout.into());
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
        assert_variant!(state, State::Associated { link_state: LinkState::LinkUp { .. }, .. });

        // Any timeout is ignored
        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);
        assert_variant!(state, State::Associated { link_state: LinkState::LinkUp { .. }, .. });
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

    #[test]
    fn connection_ping() {
        let mut h = TestHelper::new();

        let (cmd, _receiver) = connect_command_one();

        // Start in an "Associating" state
        let state = State::Associating { cfg: ClientConfig::default(), cmd };
        let assoc_conf = create_assoc_conf(fidl_mlme::AssociateResultCodes::Success);
        let state = state.on_mlme_event(assoc_conf, &mut h.context);

        // Discard AssociationSuccess and ConnectFinished info events
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::AssociationSuccess { .. })));
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::ConnectFinished { .. })));

        // Verify ping timeout is scheduled
        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        let first_ping = assert_variant!(timed_event.event.clone(), Event::ConnectionPing(info) => {
            assert_eq!(info.connected_since, info.now);
            assert!(info.last_reported.is_none());
            info
        });
        // Verify that ping is reported
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::ConnectionPing(ref info))) => {
            assert_eq!(info.connected_since, info.now);
            assert!(info.last_reported.is_none());
        });

        // Trigger the above timeout
        let _state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);

        // Verify ping timeout is scheduled again
        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        assert_variant!(timed_event.event, Event::ConnectionPing(ref info) => {
            assert_variant!(info.last_reported, Some(time) => assert_eq!(time, first_ping.now));
        });
        // Verify that ping is reported
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::ConnectionPing(ref info))) => {
            assert_variant!(info.last_reported, Some(time) => assert_eq!(time, first_ping.now));
        });
    }

    #[test]
    fn lost_connection_reported_on_deauth_ind() {
        let mut h = TestHelper::new();
        let state = link_up_state(Box::new(unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])));

        let deauth_ind = MlmeEvent::DeauthenticateInd {
            ind: fidl_mlme::DeauthenticateIndication {
                peer_sta_address: [0, 0, 0, 0, 0, 0],
                reason_code: fidl_mlme::ReasonCode::UnspecifiedReason,
            },
        };

        let _state = state.on_mlme_event(deauth_ind, &mut h.context);
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::ConnectionLost(info))) => {
            assert_eq!(info.last_rssi, 60);
        });
    }

    #[test]
    fn lost_connection_reported_on_disassoc_ind() {
        let mut h = TestHelper::new();
        let state = link_up_state(Box::new(unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])));

        let deauth_ind = MlmeEvent::DisassociateInd {
            ind: fidl_mlme::DisassociateIndication {
                peer_sta_address: [0, 0, 0, 0, 0, 0],
                reason_code: 1,
            },
        };

        let _state = state.on_mlme_event(deauth_ind, &mut h.context);
        assert_variant!(h.info_stream.try_next(), Ok(Some(InfoEvent::ConnectionLost(info))) => {
            assert_eq!(info.last_rssi, 60);
        });
    }

    #[test]
    fn bss_channel_switch_ind() {
        let mut h = TestHelper::new();
        let state = link_up_state(Box::new(unprotected_bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])));

        let switch_ind =
            MlmeEvent::OnChannelSwitched { info: fidl_mlme::ChannelSwitchInfo { new_channel: 36 } };

        assert_variant!(&state, State::Associated { bss, ..} => {
            assert_eq!(bss.chan.primary, 1);
        });
        let state = state.on_mlme_event(switch_ind, &mut h.context);
        assert_variant!(state, State::Associated { bss, ..} => {
            assert_eq!(bss.chan.primary, 36);
        });
    }

    struct TestHelper {
        mlme_stream: MlmeStream,
        info_stream: InfoStream,
        time_stream: TimeStream,
        context: Context,
        // Inspector is kept so that root node doesn't automatically get removed from VMO
        _inspector: Inspector,
    }

    impl TestHelper {
        fn new() -> Self {
            let (mlme_sink, mlme_stream) = mpsc::unbounded();
            let (info_sink, info_stream) = mpsc::unbounded();
            let (timer, time_stream) = timer::create_timer();
            let inspector = Inspector::new();
            let context = Context {
                device_info: Arc::new(fake_device_info()),
                mlme_sink: MlmeSink::new(mlme_sink),
                timer,
                att_id: 0,
                inspect: Arc::new(inspect::SmeTree::new(inspector.root())),
                info: InfoReporter::new(InfoSink::new(info_sink)),
            };
            TestHelper { mlme_stream, info_stream, time_stream, context, _inspector: inspector }
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
        let eapol_ind = create_eapol_ind(bssid.clone(), test_utils::eapol_key_frame().into());
        state.on_mlme_event(eapol_ind, &mut helper.context)
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
        assert_variant!(h.mlme_stream.try_next(), Ok(Some(MlmeRequest::Deauthenticate(req))) => {
            assert_eq!(connect_command_one().0.bss.bssid, req.peer_sta_address);
        });

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
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(req))) => {
            assert_eq!(ssid, &req.selected_bss.ssid[..])
        });
    }

    fn expect_set_ctrl_port(
        mlme_stream: &mut MlmeStream,
        bssid: [u8; 6],
        state: fidl_mlme::ControlledPortState,
    ) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetCtrlPort(req))) => {
            assert_eq!(req.peer_sta_address, bssid);
            assert_eq!(req.state, state);
        });
    }

    fn expect_auth_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        // (sme->mlme) Expect an AuthenticateRequest
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Authenticate(req))) => {
            assert_eq!(bssid, req.peer_sta_address)
        });
    }

    fn expect_deauth_req(
        mlme_stream: &mut MlmeStream,
        bssid: [u8; 6],
        reason_code: fidl_mlme::ReasonCode,
    ) {
        // (sme->mlme) Expect a DeauthenticateRequest
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Deauthenticate(req))) => {
            assert_eq!(bssid, req.peer_sta_address);
            assert_eq!(reason_code, req.reason_code);
        });
    }

    fn expect_assoc_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Associate(req))) => {
            assert_eq!(bssid, req.peer_sta_address);
        });
    }

    fn expect_eapol_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Eapol(req))) => {
            assert_eq!(req.src_addr, fake_device_info().addr);
            assert_eq!(req.dst_addr, bssid);
            assert_eq!(req.data, Vec::<u8>::from(test_utils::eapol_key_frame()));
        });
    }

    fn expect_set_ptk(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetKeys(set_keys_req))) => {
            assert_eq!(set_keys_req.keylist.len(), 1);
            let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, vec![0xCCu8; test_utils::cipher().tk_bytes().unwrap()]);
            assert_eq!(k.key_id, 0);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
            assert_eq!(k.address, bssid);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
            assert_eq!(k.cipher_suite_type, 4);
        });
    }

    fn expect_set_gtk(mlme_stream: &mut MlmeStream) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetKeys(set_keys_req))) => {
            assert_eq!(set_keys_req.keylist.len(), 1);
            let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, test_utils::gtk_bytes());
            assert_eq!(k.key_id, 2);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Group);
            assert_eq!(k.address, [0xFFu8; 6]);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
            assert_eq!(k.cipher_suite_type, 4);
        });
    }

    fn expect_set_wpa1_ptk(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetKeys(set_keys_req))) => {
            assert_eq!(set_keys_req.keylist.len(), 1);
            let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, vec![0xCCu8; test_utils::wpa1_cipher().tk_bytes().unwrap()]);
            assert_eq!(k.key_id, 0);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
            assert_eq!(k.address, bssid);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x50, 0xF2]);
            assert_eq!(k.cipher_suite_type, 2);
        });
    }

    fn expect_set_wpa1_gtk(mlme_stream: &mut MlmeStream) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetKeys(set_keys_req))) => {
            assert_eq!(set_keys_req.keylist.len(), 1);
            let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, test_utils::wpa1_gtk_bytes());
            assert_eq!(k.key_id, 2);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Group);
            assert_eq!(k.address, [0xFFu8; 6]);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x50, 0xF2]);
            assert_eq!(k.cipher_suite_type, 2);
        });
    }

    fn expect_set_wep_key(mlme_stream: &mut MlmeStream, bssid: [u8; 6], key_bytes: Vec<u8>) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::SetKeys(set_keys_req))) => {
            assert_eq!(set_keys_req.keylist.len(), 1);
            let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, &key_bytes[..]);
            assert_eq!(k.key_id, 0);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
            assert_eq!(k.address, bssid);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
            assert_eq!(k.cipher_suite_type, 1);
        });
    }

    fn expect_result<T>(mut receiver: oneshot::Receiver<T>, expected_result: T)
    where
        T: PartialEq + ::std::fmt::Debug,
    {
        assert_eq!(Ok(Some(expected_result)), receiver.try_recv());
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

    fn connect_command_wep() -> (ConnectCommand, oneshot::Receiver<ConnectResult>) {
        let (responder, receiver) = Responder::new();
        let cmd = ConnectCommand {
            bss: Box::new(fake_wep_bss_description(b"wep".to_vec())),
            responder: Some(responder),
            protection: Protection::Wep(wep_deprecated::Key::Bits40([3; 5])),
            radio_cfg: RadioConfig::default(),
        };
        (cmd, receiver)
    }

    fn connect_command_wpa1(
        supplicant: MockSupplicant,
    ) -> (ConnectCommand, oneshot::Receiver<ConnectResult>) {
        let (responder, receiver) = Responder::new();
        let wpa_ie = make_wpa1_ie();
        let cmd = ConnectCommand {
            bss: Box::new(fake_wpa1_bss_description(b"wpa1".to_vec())),
            responder: Some(responder),
            protection: Protection::LegacyWpa(Rsna {
                negotiated_protection: NegotiatedProtection::from_legacy_wpa(&wpa_ie)
                    .expect("invalid NegotiatedProtection"),
                supplicant: Box::new(supplicant),
            }),
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
                negotiated_protection: NegotiatedProtection::from_rsne(&rsne)
                    .expect("invalid NegotiatedProtection"),
                supplicant: Box::new(supplicant),
            }),
            radio_cfg: RadioConfig::default(),
        };
        (cmd, receiver)
    }

    fn idle_state() -> State {
        State::Idle { cfg: ClientConfig::default() }
    }

    fn assert_idle(state: State) {
        assert_variant!(&state, State::Idle { cfg } => { assert_eq!(state.cfg(), cfg) });
    }

    fn joining_state(cmd: ConnectCommand) -> State {
        State::Joining { cfg: ClientConfig::default(), cmd }
    }

    fn assert_joining(state: State, bss: &BssDescription) {
        assert_variant!(&state, State::Joining { cfg, cmd } => {
            assert_eq!(cfg, state.cfg());
            assert_eq!(cmd.bss.as_ref(), bss);
        });
    }

    fn authenticating_state(cmd: ConnectCommand) -> State {
        State::Authenticating { cfg: ClientConfig::default(), cmd }
    }

    fn associating_state(cmd: ConnectCommand) -> State {
        State::Associating { cfg: ClientConfig::default(), cmd }
    }

    fn assert_associating(state: State, bss: &BssDescription) {
        assert_variant!(&state, State::Associating { cfg, cmd } => {
            assert_eq!(cfg, state.cfg());
            assert_eq!(cmd.bss.as_ref(), bss);
        });
    }

    fn establishing_rsna_state(cmd: ConnectCommand) -> State {
        let rsna = assert_variant!(cmd.protection, Protection::Rsna(rsna) => rsna);
        State::Associated {
            cfg: ClientConfig::default(),
            bss: cmd.bss,
            last_rssi: 60,
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
            cfg: ClientConfig::default(),
            bss,
            last_rssi: 60,
            link_state: LinkState::LinkUp {
                protection: Protection::Open,
                since: now(),
                ping_event: None,
            },
            radio_cfg: RadioConfig::default(),
        }
    }

    fn link_up_state_protected(supplicant: MockSupplicant, bssid: [u8; 6]) -> State {
        let bss = protected_bss(b"foo".to_vec(), bssid);
        let rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
        let rsna = Rsna {
            negotiated_protection: NegotiatedProtection::from_rsne(&rsne)
                .expect("invalid NegotiatedProtection"),
            supplicant: Box::new(supplicant),
        };
        State::Associated {
            cfg: ClientConfig::default(),
            bss: Box::new(bss),
            last_rssi: 60,
            link_state: LinkState::LinkUp {
                protection: Protection::Rsna(rsna),
                since: now(),
                ping_event: None,
            },
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
