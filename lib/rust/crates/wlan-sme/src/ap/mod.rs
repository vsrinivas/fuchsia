// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aid;
mod remote_client;
mod rsn;

use failure::{bail, ensure};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent};
use futures::channel::mpsc;
use log::{debug, info, error, warn};
use wlan_rsn::{
    Authenticator,
    key::exchange::Key,
    nonce::NonceReader,
    rsna::{UpdateSink, SecAssocUpdate},
    rsne::Rsne
};

use crate::ap::{
    aid::AssociationId,
    rsn::{create_wpa2_psk_rsne, is_valid_rsne_subset},
};
use crate::{DeviceInfo, MacAddr, MlmeRequest, Ssid};
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

enum State<T: Tokens> {
    Idle {
        device_info: DeviceInfo,
        mlme_sink: MlmeSink,
        user_sink: UserSink<T>,
    },
    Started {
        bss: InfraBss<T>,
    }
}

#[derive(Clone)]
struct RsnCfg {
    password: Vec<u8>,
    rsne: Rsne,
}

struct InfraBss<T: Tokens> {
    ssid: Ssid,
    rsn_cfg: Option<RsnCfg>,
    client_map: remote_client::Map,
    device_info: DeviceInfo,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
}

pub struct ApSme<T: Tokens> {
    state: Option<State<T>>,
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
    pub fn new(device_info: DeviceInfo) -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = ApSme {
            state: Some(State::Idle {
                device_info,
                mlme_sink: MlmeSink::new(mlme_sink),
                user_sink: UserSink::new(user_sink),
            })
        };
        (sme, mlme_stream, user_stream)
    }

    pub fn on_start_command(&mut self, config: Config, token: T::StartToken) {
        self.state = self.state.take().map(|mut state| match state {
            State::Idle { device_info, mlme_sink, user_sink } => {
                let rsn_cfg = create_rsn_cfg(config.password.clone());
                let req = create_start_request(&config, rsn_cfg.as_ref());
                mlme_sink.send(MlmeRequest::StartAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the start request succeeded immediately
                user_sink.send(UserEvent::StartComplete {
                    token,
                    result: StartResult::Success,
                });
                State::Started {
                    bss: InfraBss {
                        ssid: config.ssid,
                        rsn_cfg,
                        client_map: Default::default(),
                        device_info,
                        mlme_sink,
                        user_sink,
                    }
                }
            }
            State::Started { bss: InfraBss { ref mut user_sink, .. } } => {
                let result = StartResult::AlreadyStarted;
                user_sink.send(UserEvent::StartComplete { token, result });
                state
            }
        });
    }

    pub fn on_stop_command(&mut self, token: T::StopToken) {
        self.state = self.state.take().map(|mut state| match state {
            State::Idle { ref mut user_sink, .. } => {
                user_sink.send(UserEvent::StopComplete { token });
                state
            },
            State::Started { bss } => {
                let req = fidl_mlme::StopRequest { ssid: bss.ssid.clone() };
                bss.mlme_sink.send(MlmeRequest::StopAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the stop request succeeded immediately
                bss.user_sink.send(UserEvent::StopComplete { token });
                State::Idle {
                    device_info: bss.device_info,
                    mlme_sink: bss.mlme_sink,
                    user_sink: bss.user_sink,
                }
            }
        });
    }
}

impl<T: Tokens> super::Station for ApSme<T> {
    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", event);
        self.state.as_mut().map(|state| match state {
            State::Idle { .. } => warn!("received MlmeEvent while ApSme is idle {:?}", event),
            State::Started { ref mut bss } => match event {
                MlmeEvent::AuthenticateInd { ind } => bss.handle_auth_ind(ind),
                MlmeEvent::AssociateInd { ind } => bss.handle_assoc_ind(ind),
                MlmeEvent::EapolInd { ind } => {
                    let _ = bss.handle_eapol_ind(ind).map_err(|e| warn!("{}", e));
                }
                MlmeEvent::EapolConf { resp } => {
                    if resp.result_code != fidl_mlme::EapolResultCodes::Success {
                        // TODO(NET-1634) - Handle unsuccessful EAPoL confirmation. It doesn't
                        //                  include client address, though. Maybe we can just ignore
                        //                  these messages and just set a handshake timeout instead
                        info!("Received unsuccessful EapolConf");
                    }
                }
                _ => warn!("unsupported MlmeEvent type {:?}; ignoring", event),
            }
        });
    }
}

impl<T: Tokens> InfraBss<T> {
    fn handle_auth_ind(&self, ind: fidl_mlme::AuthenticateIndication) {
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

    fn handle_assoc_ind(&mut self, ind: fidl_mlme::AssociateIndication) {
        let result = match (ind.rsn.as_ref(), self.rsn_cfg.clone()) {
            (Some(s_rsne_bytes), Some(a_rsn)) =>
                self.handle_rsn_assoc_ind(s_rsne_bytes, a_rsn, &ind.peer_sta_address),
            (None, None) => self.add_client(ind.peer_sta_address.clone(), None),
            _ => {
                warn!("unexpected RSN element from client: {:?}", ind.rsn);
                Err(fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch)
            }
        };
        let (aid, result_code) = match result {
            Ok(aid) => (aid, fidl_mlme::AssociateResultCodes::Success),
            Err(result_code) => (0, result_code),
        };
        let resp = fidl_mlme::AssociateResponse {
            peer_sta_address: ind.peer_sta_address.clone(),
            result_code,
            association_id: aid,
        };

        self.mlme_sink.send(MlmeRequest::AssocResponse(resp));
        if result_code == fidl_mlme::AssociateResultCodes::Success && self.rsn_cfg.is_some() {
            self.initiate_key_exchange(&ind.peer_sta_address);
        }
    }

    fn handle_rsn_assoc_ind(&mut self, s_rsne_bytes: &Vec<u8>, a_rsn: RsnCfg, client_addr: &MacAddr)
                            -> Result<AssociationId, fidl_mlme::AssociateResultCodes> {
        let s_rsne = wlan_rsn::rsne::from_bytes(s_rsne_bytes).to_full_result().map_err(|e| {
            warn!("failed to deserialize RSNE: {:?}", e);
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        })?;
        validate_s_rsne(&s_rsne, &a_rsn.rsne).map_err(|_| {
            warn!("incompatible client RSNE");
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        })?;

        // Note: There should be one Reader per device, not per SME.
        // Follow-up with improving on this.
        let nonce_rdr = NonceReader::new(&self.device_info.addr[..]).map_err(|e| {
            warn!("failed to create NonceReader: {}", e);
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        })?;
        let authenticator =
            Authenticator::new_wpa2psk_ccmp128(nonce_rdr,
                                               &self.ssid, &a_rsn.password, client_addr.clone(),
                                               s_rsne, self.device_info.addr, a_rsn.rsne)
                .map_err(|e| {
                    warn!("failed to create authenticator: {}", e);
                    fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
                })?;
        self.add_client(client_addr.clone(), Some(authenticator))
    }

    fn add_client(&mut self, addr: MacAddr, auth: Option<Authenticator>)
                  -> Result<AssociationId, fidl_mlme::AssociateResultCodes> {
        self.client_map.add_client(addr, auth).map_err(|e| {
            warn!("unable to add user to client map: {}", e);
            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified
        })
    }

    fn handle_eapol_ind(&mut self, ind: fidl_mlme::EapolIndication) -> Result<(), failure::Error> {
        let client_addr = &ind.src_addr;
        let authenticator = match self.client_map.get_mut_client(client_addr)
                                                 .and_then(|c| c.authenticator.as_mut()) {
            Some(authenticator) => authenticator,
            None => bail!("authenticator not found for {:?}; ignoring EapolInd msg", client_addr),
        };
        let mic_size = authenticator.get_negotiated_rsne().mic_size;
        match eapol::key_frame_from_bytes(&ind.data, mic_size).to_full_result() {
            Ok(key_frame) => {
                let frame = eapol::Frame::Key(key_frame);
                let mut update_sink = UpdateSink::default();
                match authenticator.on_eapol_frame(&mut update_sink, &frame) {
                    Ok(()) => self.process_authenticator_updates(&update_sink,client_addr),
                    Err(e) => bail!("failed processing EAPoL key frame: {}", e),
                }
            },
            Err(_) => bail!("error parsing EAPoL key frame"),
        }
        Ok(())
    }

    fn initiate_key_exchange(&mut self, client_addr: &MacAddr) {
        match self.client_map.get_mut_client(client_addr).and_then(|c| c.authenticator.as_mut()) {
            Some(authenticator) => {
                let mut update_sink = UpdateSink::default();
                match authenticator.initiate(&mut update_sink) {
                    Ok(()) => self.process_authenticator_updates(&update_sink, client_addr),
                    Err(e) => error!("error initiating key exchange: {}", e),
                }
            }
            None => error!("authenticator not found for {:?}", client_addr),
        }
    }

    fn process_authenticator_updates(&mut self, update_sink: &UpdateSink, s_addr: &MacAddr) {
        for update in update_sink {
            match update {
                SecAssocUpdate::TxEapolKeyFrame(frame) => {
                    let mut buf = Vec::with_capacity(frame.len());
                    frame.as_bytes(false, &mut buf);
                    self.mlme_sink.send(MlmeRequest::Eapol(
                        fidl_mlme::EapolRequest {
                            src_addr: self.device_info.addr.clone(),
                            dst_addr: s_addr.clone(),
                            data: buf,
                        }
                    ));
                }
                SecAssocUpdate::Key(key) => self.send_key(key, s_addr),
                _ => {}
            }
        }
    }

    fn send_key(&mut self, key: &Key, s_addr: &MacAddr) {
        let set_key_descriptor = match key {
            Key::Ptk(ptk) => {
                fidl_mlme::SetKeyDescriptor {
                    key: ptk.tk().to_vec(),
                    key_id: 0,
                    key_type: fidl_mlme::KeyType::Pairwise,
                    address: s_addr.clone(),
                    rsc: [0u8; 8],
                    cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
                    cipher_suite_type: ptk.cipher.suite_type,
                }
            }
            Key::Gtk(gtk) => {
                fidl_mlme::SetKeyDescriptor {
                    key: gtk.tk().to_vec(),
                    key_id: gtk.key_id() as u16,
                    key_type: fidl_mlme::KeyType::Group,
                    address: [0xFFu8; 6],
                    rsc: [0u8; 8],
                    cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
                    cipher_suite_type: gtk.cipher.suite_type,
                }
            }
            _ => {
                error!("unsupported key type in UpdateSink");
                return;
            }
        };
        self.mlme_sink.send(MlmeRequest::SetKeys(
            fidl_mlme::SetKeysRequest {
                keylist: vec![set_key_descriptor]
            }
        ));
    }
}

fn validate_s_rsne(s_rsne: &Rsne, a_rsne: &Rsne) -> Result<(), failure::Error> {
    ensure!(is_valid_rsne_subset(s_rsne, a_rsne)?, "incompatible client RSNE");
    Ok(())
}

fn create_rsn_cfg(password: Vec<u8>) -> Option<RsnCfg> {
    if password.is_empty() {
        None
    } else {
        Some(RsnCfg { password, rsne: create_wpa2_psk_rsne() })
    }
}

fn create_start_request(config: &Config, ap_rsn: Option<&RsnCfg>) -> fidl_mlme::StartRequest {
    let rsne_bytes = ap_rsn.as_ref().map(|RsnCfg { rsne, .. }| {
        let mut buf = Vec::with_capacity(rsne.len());
        rsne.as_bytes(&mut buf);
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
    use std::collections::HashSet;
    use std::error::Error;

    use crate::{MlmeStream, Station};

    const AP_ADDR: [u8; 6] = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
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
            password: vec![0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68],
            channel: 11,
        }
    }

    #[test]
    fn authenticate_while_sme_is_idle() {
        let (mut sme, mut mlme_stream, _) = create_sme();
        sme.on_mlme_event(create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        match mlme_stream.try_next() {
            Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
            _ => panic!("unexpected event in mlme stream"),
        }
    }

    #[test]
    fn ap_starting() {
        let (mut sme, mut mlme_stream, _) = create_sme();
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
            assert_eq!(assoc_resp.association_id, 1);
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
            assert_eq!(assoc_resp.association_id, 1);
        } else {
            panic!("expect assoc response to MLME");
        }

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::Eapol(eapol_req) = msg {
            assert_eq!(eapol_req.src_addr, AP_ADDR);
            assert_eq!(eapol_req.dst_addr, CLIENT_ADDR);
            assert!(eapol_req.data.len() > 0);
        } else {
            panic!("expect eapol request to MLME");
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

    // TODO(NET-1585) add test case for multiple clients

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
        let (mut sme, mut mlme_stream, event_stream) = create_sme();
        let config = if protected { protected_config() } else { unprotected_config() };
        sme.on_start_command(config, 10);
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::StartAp(..) => {} // expected path
            _ => panic!("expect start AP to MLME"),
        }
        (sme, mlme_stream, event_stream)
    }

    fn create_sme() -> (ApSme<FakeTokens>, MlmeStream, UserStream<FakeTokens>) {
        ApSme::new(DeviceInfo {
            supported_channels: HashSet::new(),
            addr: AP_ADDR,
        })
    }

    struct FakeTokens;
    impl Tokens for FakeTokens {
        type StartToken = i32;
        type StopToken = i32;
    }
}