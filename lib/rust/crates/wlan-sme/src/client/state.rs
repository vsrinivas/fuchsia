// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::{self, BssDescription, MlmeEvent};
use super::{ConnectResult, Tokens};
use super::internal::{MlmeSink, UserSink};
use super::super::MlmeRequest;

const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

pub enum LinkState {
    _ShakingHands,
    LinkUp
}

pub struct ConnectCommand<T> {
    pub bss: Box<BssDescription>,
    pub token: Option<T>
}

pub enum State<T: Tokens> {
    Idle,
    Joining {
        cmd: ConnectCommand<T::ConnectToken>,
    },
    Authenticating {
        cmd: ConnectCommand<T::ConnectToken>,
    },
    Associating {
        cmd: ConnectCommand<T::ConnectToken>,
    },
    Associated {
        bss: Box<BssDescription>,
        last_rssi: Option<i8>,
        link_state: LinkState,
    },
    Deauthenticating {
        // Network to join after the deauthentication process is finished
        next_cmd: Option<ConnectCommand<T::ConnectToken>>,
    }
}

impl<T: Tokens> State<T> {
    pub fn on_mlme_event(self, event: MlmeEvent, mlme_sink: &MlmeSink,
                         user_sink: &UserSink<T>) -> Self {
        match self {
            State::Idle => {
                eprintln!("Unexpected MLME message while Idle: {:?}", event);
                State::Idle
            },
            State::Joining{ cmd } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        mlme_sink.send(MlmeRequest::Authenticate(
                            fidl_mlme::AuthenticateRequest {
                                peer_sta_address: cmd.bss.bssid.clone(),
                                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                            }));
                        State::Authenticating { cmd }
                    },
                    other => {
                        eprintln!("Join request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => {
                    State::Joining{ cmd }
                }
            },
            State::Authenticating{ cmd } => match event {
                MlmeEvent::AuthenticateConf { resp } => match resp.result_code {
                    fidl_mlme::AuthenticateResultCodes::Success => {
                        to_associating_state(cmd, mlme_sink)
                    },
                    other => {
                        eprintln!("Authenticate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => State::Authenticating{ cmd }
            },
            State::Associating{ cmd } => match event {
                MlmeEvent::AssociateConf { resp } => match resp.result_code {
                    fidl_mlme::AssociateResultCodes::Success => {
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Success);
                        State::Associated {
                            bss: cmd.bss,
                            last_rssi: None,
                            link_state: LinkState::LinkUp
                        }
                    },
                    other => {
                        eprintln!("Associate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => State::Associating{ cmd }
            },
            State::Associated { bss, last_rssi, link_state } => match event {
                MlmeEvent::DisassociateInd{ .. } => {
                    let cmd = ConnectCommand{ bss, token: None };
                    to_associating_state(cmd, mlme_sink)
                },
                MlmeEvent::DeauthenticateInd{ .. } => {
                    State::Idle
                },
                MlmeEvent::SignalReport{ ind } => {
                    State::Associated {
                        bss,
                        last_rssi: Some(ind.rssi_dbm),
                        link_state
                    }
                },
                // TODO(gbonik): Also handle EapolInd and EapolConf
                _ => State::Associated{ bss, last_rssi, link_state }
            },
            State::Deauthenticating{ next_cmd } => match event {
                MlmeEvent::DeauthenticateConf{ .. } => {
                    disconnect_or_join(next_cmd, mlme_sink)
                },
                _ => State::Deauthenticating { next_cmd }
            },
        }
    }

    pub fn disconnect(self, next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                      mlme_sink: &MlmeSink, user_sink: &UserSink<T>) -> Self {
        match self {
            State::Idle => {
                disconnect_or_join(next_bss_to_join, mlme_sink)
            },
            State::Joining { cmd } | State::Authenticating { cmd }  => {
                report_connect_finished(cmd.token, user_sink, ConnectResult::Canceled);
                disconnect_or_join(next_bss_to_join, mlme_sink)
            },
            State::Associating{ cmd } => {
                report_connect_finished(cmd.token, user_sink, ConnectResult::Canceled);
                to_deauthenticating_state(cmd.bss, next_bss_to_join, mlme_sink)
            },
            State::Associated { bss, ..} => {
                to_deauthenticating_state(bss, next_bss_to_join, mlme_sink)
            },
            State::Deauthenticating { next_cmd } => {
                if let Some(next_cmd) = next_cmd {
                    report_connect_finished(next_cmd.token, user_sink, ConnectResult::Canceled);
                }
                State::Deauthenticating {
                    next_cmd: next_bss_to_join
                }
            }
        }
    }
}

fn to_deauthenticating_state<T>(current_bss: Box<BssDescription>,
                                next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                                mlme_sink: &MlmeSink) -> State<T>
    where T: Tokens
{
    mlme_sink.send(MlmeRequest::Deauthenticate(
        fidl_mlme::DeauthenticateRequest {
            peer_sta_address: current_bss.bssid.clone(),
            reason_code: fidl_mlme::ReasonCode::StaLeaving,
        }
    ));
    State::Deauthenticating {
        next_cmd: next_bss_to_join
    }
}

fn disconnect_or_join<T>(next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                         mlme_sink: &MlmeSink)
    -> State<T>
    where T: Tokens
{
    match next_bss_to_join {
        Some(next_cmd) => {
            mlme_sink.send(MlmeRequest::Join(
                fidl_mlme::JoinRequest {
                    selected_bss: clone_bss_desc(&next_cmd.bss),
                    join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
                    nav_sync_delay: 0,
                    op_rate_set: vec![]
                }
            ));
            State::Joining { cmd: next_cmd }
        },
        None => State::Idle
    }
}

fn to_associating_state<T>(cmd: ConnectCommand<T::ConnectToken>, mlme_sink: &MlmeSink)
    -> State<T>
    where T: Tokens
{
    mlme_sink.send(MlmeRequest::Associate(
        fidl_mlme::AssociateRequest {
            peer_sta_address: cmd.bss.bssid.clone(),
            rsn: get_rsn(&cmd.bss),
        }
    ));
    State::Associating { cmd }
}

fn report_connect_finished<T>(token: Option<T::ConnectToken>,
                              user_sink: &UserSink<T>, result: ConnectResult)
    where T: Tokens
{
    if let Some(token) = token {
        user_sink.send(super::UserEvent::ConnectFinished {
            token,
            result
        })
    }
}

fn clone_bss_desc(d: &fidl_mlme::BssDescription) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: d.bssid.clone(),
        ssid: d.ssid.clone(),
        bss_type: d.bss_type,
        beacon_period: d.beacon_period,
        dtim_period: d.dtim_period,
        timestamp: d.timestamp,
        local_time: d.local_time,
        rsn: d.rsn.clone(),
        chan: fidl_mlme::WlanChan {
            primary: d.chan.primary,
            cbw: d.chan.cbw,
            secondary80: d.chan.secondary80,
        },
        rssi_dbm: d.rssi_dbm,
        rcpi_dbmh: d.rcpi_dbmh,
        rsni_dbh: d.rsni_dbh
    }
}

fn get_rsn(_bss_desc: &fidl_mlme::BssDescription) -> Option<Vec<u8>> {
    // TODO(gbonik): Use wlan-rsn/eapol
    None
}