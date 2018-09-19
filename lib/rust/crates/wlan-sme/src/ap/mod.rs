// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod rsn;

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent};
use futures::channel::mpsc;
use log::{debug, warn};
use wlan_rsn::rsne::Rsne;

use crate::ap::rsn::{create_wpa2_psk_rsne, is_valid_rsne_subset};
use crate::{MlmeRequest, Ssid};
use crate::sink::MlmeSink;

const DEFAULT_BEACON_PERIOD: u16 = 100;
const DEFAULT_DTIM_PERIOD: u8 = 1;

// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {
    type StartToken;
    type StopToken;
}

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub ssid: Ssid,
    pub password: Vec<u8>,
    pub channel: u8
}

mod internal {
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}
use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

enum State {
    Idle,
    Started {
        ssid: Ssid,
        rsne: Option<Rsne>,
    }
}

pub struct ApSme<T: Tokens> {
    state: State,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
}

#[derive(Debug)]
pub enum StartResult {
    Success,
    AlreadyStarted,
    InternalError,
}

// A message from the Ap to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    StartComplete {
        token: T::StartToken,
        result: StartResult,
    },
    StopComplete {
        token: T::StopToken,
    }
}

impl<T: Tokens> ApSme<T> {
    pub fn new() -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = ApSme {
            state: State::Idle,
            mlme_sink: MlmeSink::new(mlme_sink),
            user_sink: UserSink::new(user_sink),
        };
        (sme, mlme_stream, user_stream)
    }

    pub fn on_start_command(&mut self, config: Config, token: T::StartToken) {
        match self.state {
            State::Idle => {
                let rsne = if config.password.is_empty() { None }
                           else { Some(create_wpa2_psk_rsne()) };
                let req = create_start_request(&config, rsne.as_ref());
                self.mlme_sink.send(MlmeRequest::StartAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the start request succeeded immediately
                self.user_sink.send(UserEvent::StartComplete {
                    token,
                    result: StartResult::Success,
                });
                self.state = State::Started {
                    ssid: config.ssid,
                    rsne,
                };
            },
            State::Started { .. } => {
                let result = StartResult::AlreadyStarted;
                self.user_sink.send(UserEvent::StartComplete { token, result });
            }
        }
    }

    pub fn on_stop_command(&mut self, token: T::StopToken) {
        match &self.state {
            State::Idle => {
                self.user_sink.send(UserEvent::StopComplete { token });
            },
            State::Started { ssid, .. } => {
                let req = fidl_mlme::StopRequest { ssid: ssid.clone() };
                self.mlme_sink.send(MlmeRequest::StopAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the stop request succeeded immediately
                self.user_sink.send(UserEvent::StopComplete { token });
                self.state = State::Idle;
            }
        }
    }
}

impl<T: Tokens> super::Station for ApSme<T> {
    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", event);
        match self.state {
            State::Idle => warn!("received MlmeEvent while ApSme is idle {:?}", event),
            State::Started { rsne: ref a_rsne, .. } => match event {
                MlmeEvent::AuthenticateInd { ind } => {
                    let result_code = if ind.auth_type == fidl_mlme::AuthenticationTypes::OpenSystem {
                        fidl_mlme::AuthenticateResultCodes::Success
                    } else {
                        warn!("unsupported authentication type {:?}", ind.auth_type);
                        fidl_mlme::AuthenticateResultCodes::Refused
                    };
                    let resp = fidl_mlme::AuthenticateResponse {
                        peer_sta_address: ind.peer_sta_address,
                        result_code,
                    };
                    self.mlme_sink.send(MlmeRequest::AuthResponse(resp));
                }
                MlmeEvent::AssociateInd { ind } => {
                    let resp = fidl_mlme::AssociateResponse {
                        peer_sta_address: ind.peer_sta_address,
                        result_code: evaluate_s_rsne(&ind.rsn, a_rsne),
                        // TODO(NET-1465): currently MLME generates and keeps track of the
                        //                 association IDs. SME should generate instead and keep
                        //                 track of them as well.
                        association_id: 0u16,
                    };
                    self.mlme_sink.send(MlmeRequest::AssocResponse(resp));
                }
                _ => warn!("unsupported MlmeEvent type {:?}; ignoring", event),
            }
        }
    }
}

fn evaluate_s_rsne(s_rsne_bytes: &Option<Vec<u8>>, a_rsne: &Option<Rsne>)
    -> fidl_mlme::AssociateResultCodes {

    match (s_rsne_bytes, a_rsne) {
        (Some(s_rsne), Some(ref a_rsne)) => match is_valid_rsne_subset(s_rsne.as_slice(), a_rsne) {
            Ok(true) => fidl_mlme::AssociateResultCodes::Success,
            Ok(false) => fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch,
            Err(e) => {
                warn!("error validating supplicant RSNE: {}", e);
                fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
            }
        }
        (None, None) => fidl_mlme::AssociateResultCodes::Success,
        _ => {
            warn!("unexpected RSN element from supplicant: {:?}", s_rsne_bytes);
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        }
    }
}

fn create_start_request(config: &Config, rsne: Option<&Rsne>) -> fidl_mlme::StartRequest {
    let rsne_bytes = rsne.as_ref().map(|r| {
        let mut buf = Vec::with_capacity(r.len());
        r.as_bytes(&mut buf);
        buf
    });
    fidl_mlme::StartRequest {
        ssid: config.ssid.clone(),
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: config.channel,
        rsne: rsne_bytes,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use std::error::Error;

    use crate::Station;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];
    const SSID: &'static [u8] = &[0x46, 0x55, 0x43, 0x48, 0x53, 0x49, 0x41];
    const RSNE: &'static [u8] = &[
        0x30, // element id
        0x2A, // length
        0x01, 0x00, // version
        0x00, 0x0f, 0xac, 0x04, // group data cipher suite -- CCMP-128
        0x01, 0x00, // pairwise cipher suite count
        0x00, 0x0f, 0xac, 0x04, // pairwise cipher suite list -- CCMP-128
        0x01, 0x00, // akm suite count
        0x00, 0x0f, 0xac, 0x02, // akm suite list -- PSK
        0xa8, 0x04, // rsn capabilities
        0x01, 0x00, // pmk id count

        // pmk id list
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11,

        0x00, 0x0f, 0xac, 0x04, // group management cipher suite -- CCMP-128
    ];

    fn unprotected_config() -> Config {
        Config { ssid: SSID.to_vec(), password: vec![], channel: 11 }
    }

    fn protected_config() -> Config {
        Config {
            ssid: SSID.to_vec(),
            password: vec![0x61, 0x61, 0x61, 0x61, 0x61, 0x61],
            channel: 11,
        }
    }

    #[test]
    fn authenticate_while_sme_is_idle() {
        let (mut sme, mut mlme_stream, _) = ApSme::<FakeTokens>::new();
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        match mlme_stream.try_next() {
            Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
            _ => panic!("unexpected event in mlme stream"),
        }
    }

    #[test]
    fn ap_starting() {
        let (mut sme, mut mlme_stream, _) = ApSme::<FakeTokens>::new();
        sme.on_start_command(unprotected_config(), 10);

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::StartAp(start_req) = msg {
            assert_eq!(start_req.ssid, SSID.to_vec());
            assert_eq!(start_req.bss_type, fidl_mlme::BssTypes::Infrastructure);
            assert!(start_req.beacon_period != 0);
            assert!(start_req.dtim_period != 0);
            assert_eq!(start_req.channel, unprotected_config().channel);
            assert!(start_req.rsne.is_none());
        } else {
            panic!("expect start AP request to MLME");
        }
    }

    #[test]
    fn client_authenticates_supported_authentication_type() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AuthResponse(auth_resp) = msg {
            assert_eq!(auth_resp.peer_sta_address, CLIENT_ADDR);
            assert_eq!(auth_resp.result_code, fidl_mlme::AuthenticateResultCodes::Success);
        } else {
            panic!("expect auth response to MLME");
        }
    }

    #[test]
    fn client_authenticates_unsupported_authentication_type() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::FastBssTransition));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AuthResponse(auth_resp) = msg {
            assert_eq!(auth_resp.peer_sta_address, CLIENT_ADDR);
            assert_eq!(auth_resp.result_code, fidl_mlme::AuthenticateResultCodes::Refused);
        } else {
            panic!("expect auth response to MLME");
        }
    }

    #[test]
    fn client_associates_unprotected_network() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AuthResponse(..) = msg {
            // expected path
        } else {
            panic!("expect auth response to MLME");
        }

        sme.on_mlme_event(create_assoc_ind(None));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AssocResponse(assoc_resp) = msg {
            assert_eq!(assoc_resp.peer_sta_address, CLIENT_ADDR);
            assert_eq!(assoc_resp.result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(assoc_resp.association_id, 0);
        } else {
            panic!("expect assoc response to MLME");
        }
    }

    #[test]
    fn client_associates_valid_rsne() {
        let (mut sme, mut mlme_stream, _) = start_protected_ap();
        authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(create_assoc_ind(Some(RSNE.to_vec())));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AssocResponse(assoc_resp) = msg {
            assert_eq!(assoc_resp.peer_sta_address, CLIENT_ADDR);
            assert_eq!(assoc_resp.result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(assoc_resp.association_id, 0);
        } else {
            panic!("expect assoc response to MLME");
        }
    }

    #[test]
    fn client_associates_invalid_rsne() {
        let (mut sme, mut mlme_stream, _) = start_protected_ap();
        authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        let mut rsne = RSNE.to_vec();
        rsne[13] = 0x02; // change last byte of pairwise cipher, changing it from CCMP-128 to TKIP
        sme.on_mlme_event(create_assoc_ind(Some(rsne.to_vec())));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AssocResponse(assoc_resp) = msg {
            assert_eq!(assoc_resp.peer_sta_address, CLIENT_ADDR);
            assert_eq!(assoc_resp.result_code, fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch);
            assert_eq!(assoc_resp.association_id, 0);
        } else {
            panic!("expect assoc response to MLME");
        }
    }

    fn authenticate_and_drain_mlme(sme: &mut ApSme<FakeTokens>,
                                   mlme_stream: &mut crate::MlmeStream) {
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::AuthResponse(..) = msg {
            // expected path
        } else {
            panic!("expect auth response to MLME");
        }
    }

    fn create_auth_ind(auth_type: fidl_mlme::AuthenticationTypes) -> MlmeEvent {
        MlmeEvent::AuthenticateInd {
            ind: fidl_mlme::AuthenticateIndication {
                peer_sta_address: CLIENT_ADDR,
                auth_type,
            }
        }
    }

    fn create_assoc_ind(rsne: Option<Vec<u8>>) -> MlmeEvent {
        MlmeEvent::AssociateInd {
            ind: fidl_mlme::AssociateIndication {
                peer_sta_address: CLIENT_ADDR,
                listen_interval: 100,
                ssid: Some(SSID.to_vec()),
                rsn: rsne,
            }
        }
    }

    fn start_protected_ap() -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>) {
        start_ap(true)
    }

    fn start_unprotected_ap() -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>) {
        start_ap(false)
    }

    fn start_ap(protected: bool) -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>) {
        let (mut sme, mut mlme_stream, event_stream) = ApSme::<FakeTokens>::new();
        let config = if protected { protected_config() } else { unprotected_config() };
        sme.on_start_command(config, 10);
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::StartAp(..) => {} // expected path
            _ => panic!("expect start AP to MLME"),
        }
        (sme, mlme_stream, event_stream)
    }

    struct FakeTokens;
    impl Tokens for FakeTokens {
        type StartToken = i32;
        type StopToken = i32;
    }
}