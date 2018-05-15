// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::{self, BssDescription, MlmeEvent};
use std::collections::VecDeque;
use super::super::MlmeRequest;

const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

pub enum LinkState {
    ShakingHands,
    LinkUp
}

pub enum State {
    Idle,
    Joining {
        bss: Box<BssDescription>,
    },
    Authenticating {
        bss: Box<BssDescription>,
    },
    Associating {
        bss: Box<BssDescription>,
    },
    Associated {
        bss: Box<BssDescription>,
        last_rssi: Option<i8>,
        link_state: LinkState,
    },
    Deauthenticating {
        // Network to join after the deauthentication process is finished
        next_bss: Option<Box<BssDescription>>,
    }
}

impl State {
    pub fn on_mlme_event(self, event: MlmeEvent, mlme_sink: &super::MlmeSink) -> Self {
        match self {
            State::Idle => {
                eprintln!("Unexpected MLME message while Idle: {:?}", event);
                State::Idle
            },
            State::Joining{ bss } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        mlme_sink.send(MlmeRequest::Authenticate(
                            fidl_mlme::AuthenticateRequest {
                                peer_sta_address: bss.bssid.clone(),
                                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                            }));
                        State::Authenticating { bss }
                    },
                    other => {
                        eprintln!("Join request failed with result code {:?}", other);
                        State::Idle
                    }
                },
                other => {
                    State::Joining{ bss }
                }
            },
            State::Authenticating{ bss } => match event {
                MlmeEvent::AuthenticateConf { resp } => match resp.result_code {
                    fidl_mlme::AuthenticateResultCodes::Success => {
                        Self::send_associate_request(bss, mlme_sink)
                    },
                    other => {
                        eprintln!("Authenticate request failed with result code {:?}", other);
                        State::Idle
                    }
                },
                _ => State::Authenticating{ bss }
            },
            State::Associating{ bss } => match event {
                MlmeEvent::AssociateConf { resp } => match resp.result_code {
                    fidl_mlme::AssociateResultCodes::Success => {
                        State::Associated {
                            bss,
                            last_rssi: None,
                            link_state: LinkState::LinkUp
                        }
                    },
                    other => {
                        eprintln!("Associate request failed with result code {:?}", other);
                        State::Idle
                    }
                },
                _ => State::Associating{ bss }
            },
            State::Associated { bss, last_rssi, link_state } => match event {
                MlmeEvent::DisassociateInd{ ind } => {
                    Self::send_associate_request(bss, mlme_sink)
                },
                MlmeEvent::DeauthenticateInd{ ind } => {
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
            State::Deauthenticating{ next_bss } => match event {
                MlmeEvent::DeauthenticateConf{ resp } => {
                    Self::disconnect_or_join(next_bss, mlme_sink)
                },
                _ => State::Deauthenticating { next_bss }
            },
        }
    }

    pub fn disconnect(self, next_bss_to_join: Option<Box<BssDescription>>,
                      mlme_sink: &super::MlmeSink) -> Self {
        match self {
            State::Idle | State::Joining {..} | State::Authenticating {..}  => {
                Self::disconnect_or_join(next_bss_to_join, mlme_sink)
            },
            State::Associating{ bss } | State::Associated { bss, ..} => {
                mlme_sink.send(MlmeRequest::Deauthenticate(
                    fidl_mlme::DeauthenticateRequest {
                        peer_sta_address: bss.bssid.clone(),
                        reason_code: fidl_mlme::ReasonCode::StaLeaving,
                    }
                ));
                State::Deauthenticating {
                    next_bss: next_bss_to_join
                }
            },
            State::Deauthenticating { .. } => {
                State::Deauthenticating {
                    next_bss: next_bss_to_join
                }
            }
        }
    }

    fn disconnect_or_join(next_bss_to_join: Option<Box<BssDescription>>,
                          mlme_sink: &super::MlmeSink) -> Self {
        match next_bss_to_join {
            Some(next_bss) => {
                mlme_sink.send(MlmeRequest::Join(
                    fidl_mlme::JoinRequest {
                        selected_bss: clone_bss_desc(&next_bss),
                        join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
                        nav_sync_delay: 0,
                        op_rate_set: vec![]
                    }
                ));
                State::Joining { bss: next_bss }
            },
            None => State::Idle
        }
    }

    fn send_associate_request(bss: Box<BssDescription>, mlme_sink: &super::MlmeSink) -> State {
        mlme_sink.send(MlmeRequest::Associate(
            fidl_mlme::AssociateRequest {
                peer_sta_address: bss.bssid.clone(),
                rsn: get_rsn(&bss),
            }
        ));
        State::Associating { bss }
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

fn get_rsn(bss_desc: &fidl_mlme::BssDescription) -> Option<Vec<u8>> {
    // TODO(gbonik): Use wlan-rsn/eapol
    None
}