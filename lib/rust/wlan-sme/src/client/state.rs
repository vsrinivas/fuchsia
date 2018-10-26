// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription, MlmeEvent};
use log::{error, warn};
use wlan_rsn::key::exchange::Key;
use wlan_rsn::rsna::{self, SecAssocUpdate, SecAssocStatus};

use super::bss::convert_bss_description;
use crate::phy_selection::{derive_phy_cbw};
use super::{ConnectFailure, ConnectPhyParams, ConnectResult, InfoEvent, Status, Tokens};
use super::rsn::Rsna;

use crate::MlmeRequest;
use crate::client::{Context, report_connect_finished};
use crate::clone_utils::clone_bss_desc;
use crate::sink::MlmeSink;

const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

#[derive(Debug, PartialEq)]
pub enum LinkState<T: Tokens> {
    EstablishingRsna(Option<T::ConnectToken>, Box<Rsna>),
    LinkUp(Option<Box<Rsna>>)
}

#[derive(Debug, PartialEq)]
pub struct ConnectCommand<T> {
    pub bss: Box<BssDescription>,
    pub token: Option<T>,
    pub rsna: Option<Box<Rsna>>,
    pub params: ConnectPhyParams
}

#[derive(Debug)]
pub enum RsnaStatus {
    Established,
    Failed(ConnectResult),
    Unchanged,
}

#[derive(Debug, PartialEq)]
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
        link_state: LinkState<T>,
        params: ConnectPhyParams,
    },
}

impl<T: Tokens> State<T> {
    pub fn on_mlme_event(self, event: MlmeEvent, context: &mut Context<T>) -> Self {
        match self {
            State::Idle => {
                warn!("Unexpected MLME message while Idle: {:?}", event);
                State::Idle
            },
            State::Joining{ cmd } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        context.mlme_sink.send(MlmeRequest::Authenticate(
                            fidl_mlme::AuthenticateRequest {
                                peer_sta_address: cmd.bss.bssid.clone(),
                                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                            }));
                        State::Authenticating { cmd }
                    },
                    other => {
                        error!("Join request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, &context, ConnectResult::Failed,
                                                Some(ConnectFailure::JoinFailure(other)),
                        );
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
                        to_associating_state(cmd, &context.mlme_sink)
                    },
                    other => {
                        error!("Authenticate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, &context, ConnectResult::Failed,
                                                Some(ConnectFailure::AuthenticationFailure(other)),
                        );
                        State::Idle
                    }
                },
                _ => State::Authenticating{ cmd }
            },
            State::Associating{ cmd } => match event {
                MlmeEvent::AssociateConf { resp } => match resp.result_code {
                    fidl_mlme::AssociateResultCodes::Success => {
                        context.info_sink.send(
                            InfoEvent::AssociationSuccess { att_id: context.att_id });
                        match cmd.rsna {
                            Some(mut rsna) => match rsna.supplicant.start() {
                                Err(e) => {
                                    handle_supplicant_start_failure(cmd.token, cmd.bss,
                                                                    &context, e);
                                    State::Idle
                                },
                                Ok(_) => {
                                    context.info_sink.send(
                                        InfoEvent::RsnaStarted { att_id: context.att_id });

                                    State::Associated {
                                        bss: cmd.bss,
                                        last_rssi: None,
                                        link_state: LinkState::EstablishingRsna(cmd.token, rsna),
                                        params: cmd.params,
                                    }
                                }
                            },
                            None => {
                                report_connect_finished(cmd.token, &context,
                                                        ConnectResult::Success, None);
                                State::Associated {
                                    bss: cmd.bss,
                                    last_rssi: None,
                                    link_state: LinkState::LinkUp(None),
                                    params: cmd.params,
                                }
                            }
                        }
                    },
                    other => {
                        error!("Associate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, &context, ConnectResult::Failed,
                                                Some(ConnectFailure::AssociationFailure(other)),
                        );
                        State::Idle
                    }
                },
                _ => State::Associating{ cmd }
            },
            State::Associated { bss, last_rssi, link_state, params, } => match event {
                MlmeEvent::DisassociateInd{ .. } => {
                    let (token, mut rsna) = match link_state {
                        LinkState::LinkUp(rsna) => (None, rsna),
                        LinkState::EstablishingRsna(token, rsna) => (token, Some(rsna)),
                    };
                    // Client is disassociating. The ESS-SA must be kept alive but reset.
                    if let Some(rsna) = &mut rsna {
                        rsna.supplicant.reset();
                    }

                    let cmd = ConnectCommand{
                        bss,
                        token,
                        rsna,
                        params,
                    };
                    context.att_id += 1;
                    to_associating_state(cmd, &context.mlme_sink)
                },
                MlmeEvent::DeauthenticateInd{ ind } => {
                    if let LinkState::EstablishingRsna(token, _) = link_state {
                        let connect_result = deauth_code_to_connect_result(ind.reason_code);
                        report_connect_finished(token, &context, connect_result, None);
                    }
                    State::Idle
                },
                MlmeEvent::SignalReport{ ind } => {
                    State::Associated {
                        bss,
                        last_rssi: Some(ind.rssi_dbm),
                        link_state,
                        params,
                    }
                },
                MlmeEvent::EapolInd{ ref ind } if bss.rsn.is_some() => match link_state {
                    LinkState::EstablishingRsna(token, mut rsna) => {
                        match process_eapol_ind(&context.mlme_sink, &mut rsna, &ind) {
                            RsnaStatus::Established => {
                                context.info_sink.send(
                                    InfoEvent::RsnaEstablished { att_id: context.att_id });
                                report_connect_finished(token, &context,
                                                        ConnectResult::Success, None);
                                let link_state = LinkState::LinkUp(Some(rsna));
                                State::Associated { bss, last_rssi, link_state, params, }
                            },
                            RsnaStatus::Failed(result) => {
                                report_connect_finished(token, &context, result, None);
                                send_deauthenticate_request(bss, &context.mlme_sink);
                                State::Idle
                            },
                            RsnaStatus::Unchanged => {
                                let link_state = LinkState::EstablishingRsna(token, rsna);
                                State::Associated { bss, last_rssi, link_state, params, }
                            },
                        }
                    },
                    LinkState::LinkUp(Some(mut rsna)) => {
                        match process_eapol_ind(&context.mlme_sink, &mut rsna, &ind) {
                            RsnaStatus::Unchanged => {},
                            // Once re-keying is supported, the RSNA can fail in LinkUp as well
                            // and cause deauthentication.
                            s => error!("unexpected RsnaStatus in LinkUp state: {:?}", s),
                        };
                        let link_state = LinkState::LinkUp(Some(rsna));
                        State::Associated { bss, last_rssi, link_state, params, }
                    },
                    _ => panic!("expected Link to carry RSNA because bss.rsn is present"),
                }
                _ => State::Associated{ bss, last_rssi, link_state, params, }
            },
        }
    }

    pub fn connect(self, cmd: ConnectCommand<T::ConnectToken>, context: &mut Context<T>) -> Self {
        self.disconnect_internal(context);

        let (phy_to_use, cbw_to_use) = derive_phy_cbw(&cmd.bss, &cmd.params);

        context.mlme_sink.send(MlmeRequest::Join(
            fidl_mlme::JoinRequest {
                selected_bss: clone_bss_desc(&cmd.bss),
                join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
                nav_sync_delay: 0,
                op_rate_set: vec![],
                phy: phy_to_use,
                cbw: cbw_to_use,
            }
        ));
        context.att_id += 1;
        context.info_sink.send(InfoEvent::AssociationStarted { att_id: context.att_id });
        State::Joining { cmd }
    }

    pub fn disconnect(self, context: &Context<T>) -> Self {
        self.disconnect_internal(context);
        State::Idle
    }

    fn disconnect_internal(self, context: &Context<T>) {
        match self {
            State::Idle => {},
            State::Joining { cmd } | State::Authenticating { cmd }  => {
                report_connect_finished(cmd.token, &context, ConnectResult::Canceled, None);
            },
            State::Associating{ cmd, .. } => {
                report_connect_finished(cmd.token, &context, ConnectResult::Canceled, None);
                send_deauthenticate_request(cmd.bss, &context.mlme_sink);
            },
            State::Associated { bss, .. } => {
                send_deauthenticate_request(bss, &context.mlme_sink);
            },
        }
    }

    pub fn status(&self) -> Status {
        match self {
            State::Idle => Status {
                connected_to: None,
                connecting_to: None,
            },
            State::Joining { cmd }
                | State::Authenticating { cmd }
                | State::Associating { cmd, .. } =>
            {
                Status {
                    connected_to: None,
                    connecting_to: Some(cmd.bss.ssid.clone()),
                }
            },
            State::Associated { bss, link_state: LinkState::EstablishingRsna(..), .. } => Status {
                connected_to: None,
                connecting_to: Some(bss.ssid.clone()),
            },
            State::Associated { bss, link_state: LinkState::LinkUp(..), .. } => Status {
                connected_to: Some(convert_bss_description(bss)),
                connecting_to: None,
            },
        }
    }
}

fn deauth_code_to_connect_result(reason_code: fidl_mlme::ReasonCode) -> ConnectResult {
    match reason_code {
        fidl_mlme::ReasonCode::InvalidAuthentication
        | fidl_mlme::ReasonCode::Ieee8021XAuthFailed => ConnectResult::BadCredentials,
        _ => ConnectResult::Failed
    }
}

fn process_eapol_ind(mlme_sink: &MlmeSink, rsna: &mut Rsna, ind: &fidl_mlme::EapolIndication)
    -> RsnaStatus
{
    let mic_size = rsna.negotiated_rsne.mic_size;
    let eapol_pdu = &ind.data[..];
    let eapol_frame = match eapol::key_frame_from_bytes(eapol_pdu, mic_size).to_full_result() {
        Ok(key_frame) => eapol::Frame::Key(key_frame),
        Err(e) => {
            error!("received invalid EAPOL Key frame: {:?}", e);
            return RsnaStatus::Unchanged;
        }
    };

    let mut update_sink = rsna::UpdateSink::default();
    if let Err(e) = rsna.supplicant.on_eapol_frame(&mut update_sink, &eapol_frame) {
        error!("error processing EAPOL key frame: {}", e);
        return RsnaStatus::Unchanged;
    }

    let bssid = ind.src_addr;
    let sta_addr = ind.dst_addr;
    for update in update_sink {
        match update {
            // ESS Security Association requests to send an EAPOL frame.
            // Forward EAPOL frame to MLME.
            SecAssocUpdate::TxEapolKeyFrame(frame) => {
                send_eapol_frame(mlme_sink, bssid, sta_addr, frame)
            },
            // ESS Security Association derived a new key.
            // Configure key in MLME.
            SecAssocUpdate::Key(key) => {
                send_keys(mlme_sink, bssid, key)
            },
            // Received a status update.
            // TODO(hahnr): Rework this part.
            // As of now, we depend on the fact that the status is always the last update.
            // However, this fact is not clear from the API.
            // We should fix the API and make this more explicit.
            // Then we should rework this part.
            SecAssocUpdate::Status(status) => match status {
                // ESS Security Association was successfully established. Link is now up.
                SecAssocStatus::EssSaEstablished => return RsnaStatus::Established,
                // TODO(hahnr): The API should not expose whether or not the connection failed
                // because of bad credentials as it allows callers to reason about location
                // information since the network was apparently found.
                SecAssocStatus::WrongPassword => {
                    return RsnaStatus::Failed(ConnectResult::BadCredentials);
                }
            },
        }
    }

    RsnaStatus::Unchanged
}

fn send_eapol_frame(mlme_sink: &MlmeSink, bssid: [u8; 6], sta_addr: [u8; 6], frame: eapol::KeyFrame)
{
    let mut buf = Vec::with_capacity(frame.len());
    frame.as_bytes(false, &mut buf);
    mlme_sink.send(MlmeRequest::Eapol(
        fidl_mlme::EapolRequest {
            src_addr: sta_addr,
            dst_addr: bssid,
            data: buf,
        }
    ));
}

fn send_keys(mlme_sink: &MlmeSink, bssid: [u8; 6], key: Key)
{
    match key {
        Key::Ptk(ptk) => {
            mlme_sink.send(MlmeRequest::SetKeys(
                fidl_mlme::SetKeysRequest {
                    keylist: vec![fidl_mlme::SetKeyDescriptor{
                        key_type: fidl_mlme::KeyType::Pairwise,
                        key: ptk.tk().to_vec(),
                        key_id: 0,
                        address: bssid,
                        cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
                        cipher_suite_type: ptk.cipher.suite_type,
                        rsc: [0u8; 8],
                    }]
                }
            ));
        },
        Key::Gtk(gtk) => {
            mlme_sink.send(MlmeRequest::SetKeys(
                fidl_mlme::SetKeysRequest {
                    keylist: vec![fidl_mlme::SetKeyDescriptor{
                        key_type: fidl_mlme::KeyType::Group,
                        key: gtk.tk().to_vec(),
                        key_id: gtk.key_id() as u16,
                        address: [0xFFu8; 6],
                        cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
                        cipher_suite_type: gtk.cipher.suite_type,
                        rsc: [0u8; 8],
                    }]
                }
            ));
        },
        _ => error!("derived unexpected key")
    };
}

fn send_deauthenticate_request(current_bss: Box<BssDescription>,
                               mlme_sink: &MlmeSink) {
    mlme_sink.send(MlmeRequest::Deauthenticate(
        fidl_mlme::DeauthenticateRequest {
            peer_sta_address: current_bss.bssid.clone(),
            reason_code: fidl_mlme::ReasonCode::StaLeaving,
        }
    ));
}

fn to_associating_state<T>(cmd: ConnectCommand<T::ConnectToken>, mlme_sink: &MlmeSink)
    -> State<T>
    where T: Tokens
{
    let s_rsne_data = cmd.rsna.as_ref().map(|rsna| {
        let s_rsne = rsna.negotiated_rsne.to_full_rsne();
        let mut buf = Vec::with_capacity(s_rsne.len());
        s_rsne.as_bytes(&mut buf);
        buf
    });

    mlme_sink.send(MlmeRequest::Associate(
        fidl_mlme::AssociateRequest {
            peer_sta_address: cmd.bss.bssid.clone(),
            rsn: s_rsne_data,
        }
    ));
    State::Associating { cmd }
}

fn handle_supplicant_start_failure<T>(token: Option<T::ConnectToken>, bss: Box<BssDescription>,
                                      context: &Context<T>, e: failure::Error) where T: Tokens
{
    error!("deauthenticating; could not start Supplicant: {}", e);
    send_deauthenticate_request(bss, &context.mlme_sink);

    // TODO(hahnr): Report RSNA specific failure instead.
    let reason = fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified;
    report_connect_finished(token, &context,
                            ConnectResult::Failed,
                            Some(ConnectFailure::AssociationFailure(reason)));
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::channel::mpsc;
    use std::sync::Arc;

    use crate::client::test_utils::{expect_info_event, fake_unprotected_bss_description};
    use crate::client::{InfoSink, UserEvent, UserStream, UserSink};
    use crate::{DeviceInfo, InfoStream, MlmeStream, Ssid};

    #[derive(Debug, PartialEq)]
    struct FakeTokens;

    impl Tokens for FakeTokens {
        type ScanToken = u32;
        type ConnectToken = u32;
    }

    #[test]
    fn associate_happy_path_unprotected() {
        let mut h = TestHelper::new();

        let state = idle_state();

        // Issue a "connect" command
        let state = state.connect(connect_command_one(), &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::AssociationStarted { att_id: 1});

        // (sme->mlme) Expect a JoinRequest
        match h.mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Join(req)) => {
                assert_eq!(connect_command_one().bss.ssid, req.selected_bss.ssid);
            },
            other => panic!("expected a Join request, got {:?}", other),
        }

        // (mlme->sme) Send a JoinConf as a response
        let join_conf = MlmeEvent::JoinConf {
            resp: fidl_mlme::JoinConfirm {
                result_code: fidl_mlme::JoinResultCodes::Success
            }
        };

        let state = state.on_mlme_event(join_conf, &mut h.context);

        // (sme->mlme) Expect an AuthenticateRequest
        match h.mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Authenticate(req)) => {
                assert_eq!(connect_command_one().bss.bssid, req.peer_sta_address);
            },
            other => panic!("expected an Authenticate request, got {:?}", other)
        }

        // (mlme->sme) Send an AuthenticateConf as a response
        let auth_conf = MlmeEvent::AuthenticateConf {
            resp: fidl_mlme::AuthenticateConfirm {
                peer_sta_address: connect_command_one().bss.bssid,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Success,
            }
        };
        let state = state.on_mlme_event(auth_conf, &mut h.context);

        // (sme->mlme) Expect an AssociateRequest
        match h.mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Associate(req)) => {
                assert_eq!(connect_command_one().bss.bssid, req.peer_sta_address);
            },
            other => panic!("expected an Associate request, got {:?}", other)
        }

        // (mlme->sme) Send an AssociateConf
        let assoc_conf = MlmeEvent::AssociateConf {
            resp: fidl_mlme::AssociateConfirm {
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 55,
            }
        };
        let _state = state.on_mlme_event(assoc_conf, &mut h.context);

        expect_info_event(&mut h.info_stream, InfoEvent::AssociationSuccess { att_id: 1 });

        // User should be notified that we are connected
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Success);
    }

    #[test]
    fn join_failure() {
        let mut h = TestHelper::new();

        // Start in a "Joining" state
        let state = State::Joining::<FakeTokens> { cmd: connect_command_one() };

        // (mlme->sme) Send an unsuccessful JoinConf
        let join_conf = MlmeEvent::JoinConf {
            resp: fidl_mlme::JoinConfirm {
                result_code: fidl_mlme::JoinResultCodes::JoinFailureTimeout
            }
        };
        let state = state.on_mlme_event(join_conf, &mut h.context);
        assert_eq!(idle_state(), state);

        // User should be notified that connection attempt failed
        expect_connect_result(
            &mut h.user_stream,
            connect_command_one().token.unwrap(),
            ConnectResult::Failed,
        );

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

        // Start in an "Authenticating" state
        let state = State::Authenticating::<FakeTokens> { cmd: connect_command_one() };

        // (mlme->sme) Send an unsuccessful AuthenticateConf
        let auth_conf = MlmeEvent::AuthenticateConf {
            resp: fidl_mlme::AuthenticateConfirm {
                peer_sta_address: connect_command_one().bss.bssid,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
            }
        };
        let state = state.on_mlme_event(auth_conf, &mut h.context);
        assert_eq!(idle_state(), state);

        // User should be notified that connection attempt failed
        expect_connect_result(
            &mut h.user_stream,
            connect_command_one().token.unwrap(),
            ConnectResult::Failed,
        );

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

        // Start in an "Associating" state
        let state = State::Associating::<FakeTokens> { cmd: connect_command_one() };

        // (mlme->sme) Send an unsuccessful AssociateConf
        let assoc_conf = MlmeEvent::AssociateConf {
            resp: fidl_mlme::AssociateConfirm {
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                association_id: 0,
            }
        };
        let state = state.on_mlme_event(assoc_conf, &mut h.context);
        assert_eq!(idle_state(), state);

        // User should be notified that connection attempt failed
        expect_connect_result(
            &mut h.user_stream,
            connect_command_one().token.unwrap(),
            ConnectResult::Failed,
        );

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
        let state = joining_state(connect_command_one());
        let state = state.connect(connect_command_two(), &mut h.context);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream);
        assert_eq!(joining_state(connect_command_two()), state);
    }

    #[test]
    fn connect_while_authenticating() {
        let mut h = TestHelper::new();
        let state = authenticating_state(connect_command_one());
        let state = state.connect(connect_command_two(), &mut h.context);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream);
        assert_eq!(joining_state(connect_command_two()), state);
    }

    #[test]
    fn connect_while_associating() {
        let mut h = TestHelper::new();
        let state = associating_state(connect_command_one());
        let state = state.connect(connect_command_two(), &mut h.context);
        let state = exchange_deauth(state, &mut h);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        expect_join_request(&mut h.mlme_stream);
        assert_eq!(joining_state(connect_command_two()), state);
    }

    #[test]
    fn connect_while_link_up() {
        let mut h = TestHelper::new();
        let state = link_up_state(connect_command_one().bss);
        let state = state.connect(connect_command_two(), &mut h.context);
        let state = exchange_deauth(state, &mut h);
        expect_join_request(&mut h.mlme_stream);
        assert_eq!(joining_state(connect_command_two()), state);
    }

    #[test]
    fn disconnect_while_idle() {
        let mut h = TestHelper::new();
        let new_state = idle_state().disconnect(&h.context);
        assert_eq!(idle_state(), new_state);
        // Expect no messages to the MLME or the user
        assert!(h.mlme_stream.try_next().is_err());
        assert!(h.user_stream.try_next().is_err());
    }

    #[test]
    fn disconnect_while_joining() {
        let mut h = TestHelper::new();
        let state = joining_state(connect_command_one());
        let state = state.disconnect(&h.context);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        assert_eq!(idle_state(), state);
    }

    #[test]
    fn disconnect_while_authenticating() {
        let mut h = TestHelper::new();
        let state = authenticating_state(connect_command_one());
        let state = state.disconnect(&h.context);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        assert_eq!(idle_state(), state);
    }

    #[test]
    fn disconnect_while_associating() {
        let mut h = TestHelper::new();
        let state = associating_state(connect_command_one());
        let state = state.disconnect(&h.context);
        let state = exchange_deauth(state, &mut h);
        expect_connect_result(&mut h.user_stream, connect_command_one().token.unwrap(),
                              ConnectResult::Canceled);
        assert_eq!(idle_state(), state);
    }

    #[test]
    fn disconnect_while_link_up() {
        let mut h = TestHelper::new();
        let state = link_up_state(connect_command_one().bss);
        let state = state.disconnect(&h.context);
        let state = exchange_deauth(state, &mut h);
        assert_eq!(idle_state(), state);
    }

    #[test]
    fn increment_att_id_on_connect() {
        let mut h = TestHelper::new();
        let state = idle_state();
        assert_eq!(h.context.att_id, 0);

        let state = state.connect(connect_command_one(), &mut h.context);
        assert_eq!(h.context.att_id, 1);

        let state = state.disconnect(&h.context);
        assert_eq!(h.context.att_id, 1);

        let state = state.connect(connect_command_two(), &mut h.context);
        assert_eq!(h.context.att_id, 2);

        let _state = state.connect(connect_command_one(), &mut h.context);
        assert_eq!(h.context.att_id, 3);
    }

    #[test]
    fn increment_att_id_on_disassociate_ind() {
        let mut h = TestHelper::new();
        let state = link_up_state(connect_command_no_token().bss);
        assert_eq!(h.context.att_id, 0);

        let disassociate_ind = MlmeEvent::DisassociateInd {
            ind: fidl_mlme::DisassociateIndication {
                peer_sta_address: [0, 0, 0, 0, 0, 0],
                reason_code: 0,
            }
        };

        let state = state.on_mlme_event(disassociate_ind, &mut h.context);
        assert_eq!(associating_state(connect_command_no_token()), state);
        assert_eq!(h.context.att_id, 1);
    }

    struct TestHelper {
        mlme_stream: MlmeStream,
        user_stream: UserStream<FakeTokens>,
        info_stream: InfoStream,
        context: Context<FakeTokens>,
    }

    impl TestHelper {
        fn new() -> Self {
            let (mlme_sink, mlme_stream) = mpsc::unbounded();
            let (user_sink, user_stream) = mpsc::unbounded();
            let (info_sink, info_stream) = mpsc::unbounded();
            let context = Context {
                device_info: Arc::new(fake_device_info()),
                mlme_sink: MlmeSink::new(mlme_sink),
                user_sink: UserSink::new(user_sink),
                info_sink: InfoSink::new(info_sink),
                att_id: 0,
            };
            TestHelper { mlme_stream, user_stream, info_stream, context }
        }
    }

    fn exchange_deauth(state: State<FakeTokens>, h: &mut TestHelper) -> State<FakeTokens> {
        // (sme->mlme) Expect a DeauthenticateRequest
        match h.mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Deauthenticate(req)) => {
                assert_eq!(connect_command_one().bss.bssid, req.peer_sta_address);
            },
            other => panic!("expected a Deauthenticate request, got {:?}", other),
        }

        // (mlme->sme) Send a DeauthenticateConf as a response
        let deauth_conf = MlmeEvent::DeauthenticateConf {
            resp: fidl_mlme::DeauthenticateConfirm {
                peer_sta_address: connect_command_one().bss.bssid,
            }
        };
        state.on_mlme_event(deauth_conf, &mut h.context)
    }

    fn expect_join_request(mlme_stream: &mut MlmeStream) {
        // (sme->mlme) Expect a JoinRequest
        match mlme_stream.try_next().unwrap() {
            Some(MlmeRequest::Join(req)) => {
                assert_eq!(connect_command_two().bss.ssid, req.selected_bss.ssid);
            },
            other => panic!("expected a Join request, got {:?}", other),
        }
    }

    fn expect_connect_result(user_stream: &mut UserStream<FakeTokens>,
                             expected_token: u32,
                             expected_result: ConnectResult) {
        match user_stream.try_next().unwrap() {
            Some(UserEvent::ConnectFinished { token, result }) => {
                assert_eq!(expected_token, token);
                assert_eq!(expected_result, result);
            },
            other => panic!("expected a ConnectFinished event, got {:?}", other)
        }
    }

    fn connect_command_one() -> ConnectCommand<u32> {
        ConnectCommand {
            bss: Box::new(bss(b"foo".to_vec(), [7, 7, 7, 7, 7, 7])),
            token: Some(123_u32),
            rsna: None,
            params: ConnectPhyParams { phy: None, cbw: None, },
        }
    }

    fn connect_command_two() -> ConnectCommand<u32> {
        ConnectCommand {
            bss: Box::new(bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])),
            token: Some(456_u32),
            rsna: None,
            params: ConnectPhyParams { phy: None, cbw: None, },
        }
    }

    fn connect_command_no_token() -> ConnectCommand<u32> {
        ConnectCommand {
            bss: Box::new(bss(b"bar".to_vec(), [8, 8, 8, 8, 8, 8])),
            token: None,
            rsna: None,
            params: ConnectPhyParams { phy: None, cbw: None, },
        }
    }

    fn idle_state() -> State<FakeTokens> {
        State::Idle
    }

    fn joining_state(cmd: ConnectCommand<u32>) -> State<FakeTokens> {
        State::Joining { cmd }
    }

    fn authenticating_state(cmd: ConnectCommand<u32>) -> State<FakeTokens> {
        State::Authenticating { cmd }
    }

    fn associating_state(cmd: ConnectCommand<u32>) -> State<FakeTokens> {
        State::Associating { cmd }
    }

    fn link_up_state(bss: Box<fidl_mlme::BssDescription>) -> State<FakeTokens> {
        State::Associated {
            bss,
            last_rssi: None,
            link_state: LinkState::LinkUp(None),
            params: ConnectPhyParams { phy: None, cbw: None, },
        }
    }

    fn bss(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
        fidl_mlme::BssDescription {
            bssid,
            .. fake_unprotected_bss_description(ssid)
        }
    }

    fn fake_device_info() -> DeviceInfo {
        DeviceInfo {
            addr: [ 0, 1, 2, 3, 4, 5 ],
            bands: vec![],
        }
    }
}
