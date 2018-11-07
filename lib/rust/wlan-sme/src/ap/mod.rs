// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aid;
mod event;
mod remote_client;
mod rsn;

use failure::{bail, ensure};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent};
use futures::channel::mpsc;
use log::{debug, info, error, warn};
use wlan_rsn::{
    Authenticator,
    NegotiatedRsne,
    nonce::NonceReader,
    gtk::GtkProvider,
    psk,
    rsne::Rsne
};
use crate::ap::{
    aid::AssociationId,
    event::Event,
    rsn::{create_wpa2_psk_rsne, is_valid_rsne_subset},
};
use crate::{DeviceInfo, MacAddr, MlmeRequest, Ssid};
use crate::sink::MlmeSink;
use crate::timer::{self, TimedEvent, Timer};
use std::sync::{Arc, Mutex};

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
pub type TimeStream = timer::TimeStream<Event>;

enum State<T: Tokens> {
    Idle {
        device_info: DeviceInfo,
        mlme_sink: MlmeSink,
        user_sink: UserSink<T>,
        timer: Timer<Event>,
    },
    Started {
        bss: InfraBss<T>,
    }
}

#[derive(Clone)]
struct RsnCfg {
    psk: psk::Psk,
    rsne: Rsne,
}

struct InfraBss<T: Tokens> {
    ssid: Ssid,
    rsn_cfg: Option<RsnCfg>,
    client_map: remote_client::Map,
    ctx: Context<T>,
}

pub struct Context<T: Tokens> {
    device_info: DeviceInfo,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
    timer: Timer<Event>,
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
    pub fn new(device_info: DeviceInfo) -> (Self, crate::MlmeStream, UserStream<T>, TimeStream) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let (timer, time_stream) = timer::create_timer();
        let sme = ApSme {
            state: Some(State::Idle {
                device_info,
                mlme_sink: MlmeSink::new(mlme_sink),
                user_sink: UserSink::new(user_sink),
                timer,
            })
        };
        (sme, mlme_stream, user_stream, time_stream)
    }

    pub fn on_start_command(&mut self, config: Config, token: T::StartToken) {
        self.state = self.state.take().map(|mut state| match state {
            State::Idle { device_info, mlme_sink, user_sink, timer } => {
                let rsn_cfg_result = create_rsn_cfg(&config.ssid[..], &config.password[..]);
                match rsn_cfg_result {
                    Err(e) => {
                        error!("error configuring RSN: {}", e);
                        user_sink.send(UserEvent::StartComplete {
                            token,
                            result: StartResult::InternalError,
                        });
                        State::Idle { device_info, mlme_sink, user_sink, timer }
                    },
                    Ok(rsn_cfg) => {
                        let req = create_start_request(&config, rsn_cfg.as_ref());
                        mlme_sink.send(MlmeRequest::Start(req));
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
                                ctx: Context {
                                    device_info,
                                    mlme_sink,
                                    user_sink,
                                    timer,
                                }
                            }
                        }
                    }
                }
            },
            State::Started { bss: InfraBss { ref mut ctx, .. } } => {
                let result = StartResult::AlreadyStarted;
                ctx.user_sink.send(UserEvent::StartComplete { token, result });
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
                bss.ctx.mlme_sink.send(MlmeRequest::Stop(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the stop request succeeded immediately
                bss.ctx.user_sink.send(UserEvent::StopComplete { token });
                State::Idle {
                    device_info: bss.ctx.device_info,
                    mlme_sink: bss.ctx.mlme_sink,
                    user_sink: bss.ctx.user_sink,
                    timer: bss.ctx.timer,
                }
            }
        });
    }
}

impl<T: Tokens> super::Station for ApSme<T> {
    type Event = Event;

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", event);
        self.state.as_mut().map(|state| match state {
            State::Idle { .. } => warn!("received MlmeEvent while ApSme is idle {:?}", event),
            State::Started { ref mut bss } => match event {
                MlmeEvent::AuthenticateInd { ind } => bss.handle_auth_ind(ind),
                MlmeEvent::DeauthenticateInd { ind } => bss.handle_deauth(&ind.peer_sta_address),
                MlmeEvent::DeauthenticateConf { resp } =>
                    bss.handle_deauth(&resp.peer_sta_address),
                MlmeEvent::AssociateInd { ind } => bss.handle_assoc_ind(ind),
                MlmeEvent::DisassociateInd { ind } => bss.handle_disassoc_ind(ind),
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

    fn on_timeout(&mut self, timed_event: TimedEvent<Event>) {
        self.state.as_mut().map(|state| match state {
            State::Idle { .. } => debug!("received timeout while ApSme is idle"),
            State::Started { ref mut bss } => bss.handle_timeout(timed_event),
        });
    }
}

impl<T: Tokens> InfraBss<T> {
    fn handle_auth_ind(&mut self, ind: fidl_mlme::AuthenticateIndication) {
        if self.client_map.remove_client(&ind.peer_sta_address).is_some() {
            warn!("client {:?} authenticates while still associated; removed client from map",
                  ind.peer_sta_address);
        }

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
        self.ctx.mlme_sink.send(MlmeRequest::AuthResponse(resp));
    }

    fn handle_deauth(&mut self, client_addr: &MacAddr) {
        let _ = self.client_map.remove_client(client_addr);
    }

    fn handle_assoc_ind(&mut self, ind: fidl_mlme::AssociateIndication) {
        if self.client_map.remove_client(&ind.peer_sta_address).is_some() {
            warn!("client {:?} associates while still associated; removed client from map",
                  ind.peer_sta_address);
        }

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

        self.ctx.mlme_sink.send(MlmeRequest::AssocResponse(resp));
        if result_code == fidl_mlme::AssociateResultCodes::Success && self.rsn_cfg.is_some() {
            match self.client_map.get_mut_client(&ind.peer_sta_address) {
                Some(client) => client.initiate_key_exchange(&mut self.ctx, 1),
                None => error!("cannot initiate key exchange for unknown client: {:02X?}",
                               ind.peer_sta_address),
            }
        }
    }

    fn handle_disassoc_ind(&mut self, ind: fidl_mlme::DisassociateIndication) {
        let _ = self.client_map.remove_client(&ind.peer_sta_address);
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
        let nonce_rdr = NonceReader::new(&self.ctx.device_info.addr[..]).map_err(|e| {
            warn!("failed to create NonceReader: {}", e);
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        })?;
        let gtk_provider = get_gtk_provider(&s_rsne).map_err(|e| {
            warn!("failed to create GtkProvider: {}", e);
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
        })?;
        let authenticator =
            Authenticator::new_wpa2psk_ccmp128(nonce_rdr, gtk_provider,
                                               a_rsn.psk.clone(), client_addr.clone(),
                                               s_rsne, self.ctx.device_info.addr, a_rsn.rsne)
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
        match self.client_map.get_mut_client(client_addr) {
            Some(client) => client.handle_eapol_ind(ind, &mut self.ctx),
            None => bail!("client {:02X?} not found; ignoring EapolInd msg", client_addr),
        }
    }

    fn handle_timeout(&mut self, timed_event: TimedEvent<Event>) {
        let event = timed_event.event;
        if let Some(client) = self.client_map.get_mut_client(&event.addr) {
            client.handle_timeout(timed_event.id, event.event, &mut self.ctx);
        }
    }
}

fn validate_s_rsne(s_rsne: &Rsne, a_rsne: &Rsne) -> Result<(), failure::Error> {
    ensure!(is_valid_rsne_subset(s_rsne, a_rsne)?, "incompatible client RSNE");
    Ok(())
}

fn get_gtk_provider(s_rsne: &Rsne) -> Result<Arc<Mutex<GtkProvider>>, failure::Error> {
    let negotiated_rsne = NegotiatedRsne::from_rsne(&s_rsne)?;
    let gtk_provider = GtkProvider::new(negotiated_rsne.group_data)?;
    Ok(Arc::new(Mutex::new(gtk_provider)))
}

fn create_rsn_cfg(ssid: &[u8], password: &[u8]) -> Result<Option<RsnCfg>, failure::Error> {
    if password.is_empty() {
        Ok(None)
    } else {
        let psk = psk::compute(password, ssid)?;
        Ok(Some(RsnCfg { psk, rsne: create_wpa2_psk_rsne() }))
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
        mesh_id: vec![],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use std::error::Error;

    use crate::{MlmeStream, Station};

    const AP_ADDR: [u8; 6] = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];
    const CLIENT_ADDR2: [u8; 6] = [0x22, 0x22, 0x22, 0x22, 0x22, 0x22];
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
        let (mut sme, mut mlme_stream, _, _) = create_sme();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        match mlme_stream.try_next() {
            Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
            _ => panic!("unexpected event in mlme stream"),
        }
    }

    #[test]
    fn ap_starting() {
        let (mut sme, mut mlme_stream, _, _) = create_sme();
        sme.on_start_command(unprotected_config(), 10);

        let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
        if let MlmeRequest::Start(start_req) = msg {
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
        let (mut sme, mut mlme_stream, _, _) = start_unprotected_ap();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);
    }

    #[test]
    fn client_authenticates_unsupported_authentication_type() {
        let (mut sme, mut mlme_stream, _, _) = start_unprotected_ap();
        let client = Client::default();
        let auth_ind = client.create_auth_ind(fidl_mlme::AuthenticationTypes::FastBssTransition);
        sme.on_mlme_event(auth_ind);
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Refused);
    }

    #[test]
    fn client_associates_unprotected_network() {
        let (mut sme, mut mlme_stream, _, _) = start_unprotected_ap();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(&mut mlme_stream, 1, fidl_mlme::AssociateResultCodes::Success);
    }

    #[test]
    fn client_associates_valid_rsne() {
        let (mut sme, mut mlme_stream, _, _) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(&mut mlme_stream, 1, fidl_mlme::AssociateResultCodes::Success);
        client.verify_eapol_req(&mut mlme_stream);
    }

    #[test]
    fn client_associates_invalid_rsne() {
        let (mut sme, mut mlme_stream, _, _) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(&mut mlme_stream, 0,
                                 fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch);
    }

    #[test]
    fn rsn_handshake_timeout() {
        let (mut sme, mut mlme_stream, _, mut time_stream) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(&mut mlme_stream, 1, fidl_mlme::AssociateResultCodes::Success);

        for _i in 0..4 {
            client.verify_eapol_req(&mut mlme_stream);

            let (_, event) = time_stream.try_next().unwrap().expect("expect timer message");
            // Calling `on_timeout` with a different event ID is a no-op
            let mut fake_event = event.clone();
            fake_event.id += 1;
            sme.on_timeout(fake_event);
            match mlme_stream.try_next() {
                Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
                _ => panic!("unexpected event in mlme stream"),
            }
            sme.on_timeout(event);
        }

        client.verify_deauth_req(&mut mlme_stream, fidl_mlme::ReasonCode::FourwayHandshakeTimeout);
    }

    #[test]
    fn client_restarts_authentication_flow() {
        let (mut sme, mut mlme_stream, _, _) = start_unprotected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);
        client.associate_and_drain_mlme(&mut sme, &mut mlme_stream, None);

        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(&mut mlme_stream, 1, fidl_mlme::AssociateResultCodes::Success);
    }

    #[test]
    fn multiple_clients_associate() {
        let (mut sme, mut mlme_stream, _, _) = start_protected_ap();
        let client1 = Client::default();
        let client2 = Client { addr: CLIENT_ADDR2 };

        sme.on_mlme_event(client1.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client1.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client2.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client2.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client1.create_assoc_ind(Some(RSNE.to_vec())));
        client1.verify_assoc_resp(&mut mlme_stream, 1, fidl_mlme::AssociateResultCodes::Success);
        client1.verify_eapol_req(&mut mlme_stream);

        sme.on_mlme_event(client2.create_assoc_ind(Some(RSNE.to_vec())));
        client2.verify_assoc_resp(&mut mlme_stream, 2, fidl_mlme::AssociateResultCodes::Success);
        client2.verify_eapol_req(&mut mlme_stream);
    }

    struct Client {
        addr: MacAddr
    }

    impl Client {
        fn default() -> Self {
            Client { addr: CLIENT_ADDR }
        }

        fn authenticate_and_drain_mlme(&self,
                                       sme: &mut ApSme<FakeTokens>,
                                       mlme_stream: &mut crate::MlmeStream) {
            sme.on_mlme_event(self.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::AuthResponse(..) = msg {
                // expected path
            } else {
                panic!("expect auth response to MLME");
            }
        }

        fn associate_and_drain_mlme(&self,
                                    sme: &mut ApSme<FakeTokens>,
                                    mlme_stream: &mut crate::MlmeStream,
                                    rsne: Option<Vec<u8>>) {
            sme.on_mlme_event(self.create_assoc_ind(rsne));

            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::AssocResponse(..) = msg {
                // expected path
            } else {
                panic!("expect auth response to MLME");
            }
        }

        fn create_auth_ind(&self, auth_type: fidl_mlme::AuthenticationTypes) -> MlmeEvent {
            MlmeEvent::AuthenticateInd {
                ind: fidl_mlme::AuthenticateIndication {
                    peer_sta_address: self.addr,
                    auth_type,
                }
            }
        }

        fn create_assoc_ind(&self, rsne: Option<Vec<u8>>) -> MlmeEvent {
            MlmeEvent::AssociateInd {
                ind: fidl_mlme::AssociateIndication {
                    peer_sta_address: self.addr,
                    listen_interval: 100,
                    ssid: Some(SSID.to_vec()),
                    rsn: rsne,
                }
            }
        }

        fn verify_auth_resp(&self, mlme_stream: &mut MlmeStream,
                            result_code: fidl_mlme::AuthenticateResultCodes) {
            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::AuthResponse(auth_resp) = msg {
                assert_eq!(auth_resp.peer_sta_address, self.addr);
                assert_eq!(auth_resp.result_code, result_code);
            } else {
                panic!("expect auth response to MLME");
            }
        }

        fn verify_assoc_resp(&self, mlme_stream: &mut MlmeStream, aid: AssociationId,
                             result_code: fidl_mlme::AssociateResultCodes) {
            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::AssocResponse(assoc_resp) = msg {
                assert_eq!(assoc_resp.peer_sta_address, self.addr);
                assert_eq!(assoc_resp.association_id, aid);
                assert_eq!(assoc_resp.result_code, result_code);
            } else {
                panic!("expect assoc response to MLME");
            }
        }

        fn verify_eapol_req(&self, mlme_stream: &mut MlmeStream) {
            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::Eapol(eapol_req) = msg {
                assert_eq!(eapol_req.src_addr, AP_ADDR);
                assert_eq!(eapol_req.dst_addr, self.addr);
                assert!(eapol_req.data.len() > 0);
            } else {
                panic!("expect eapol request to MLME");
            }
        }

        fn verify_deauth_req(&self, mlme_stream: &mut MlmeStream,
                             reason_code: fidl_mlme::ReasonCode) {
            let msg = mlme_stream.try_next().unwrap().expect("expect mlme message");
            if let MlmeRequest::Deauthenticate(deauth_req) = msg {
                assert_eq!(deauth_req.peer_sta_address, self.addr);
                assert_eq!(deauth_req.reason_code, reason_code);
            } else {
                panic!("expect deauthenticate request to MLME");
            }
        }
    }

    fn start_protected_ap() -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>,
                                TimeStream) {
        start_ap(true)
    }

    fn start_unprotected_ap() -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>,
                                  TimeStream) {
        start_ap(false)
    }

    fn start_ap(protected: bool) -> (ApSme<FakeTokens>, crate::MlmeStream, UserStream<FakeTokens>,
                                     TimeStream) {

        let (mut sme, mut mlme_stream, event_stream, timeout_stream) = create_sme();
        let config = if protected { protected_config() } else { unprotected_config() };
        sme.on_start_command(config, 10);
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::Start(..) => {} // expected path
            _ => panic!("expect start AP to MLME"),
        }
        (sme, mlme_stream, event_stream, timeout_stream)
    }

    fn create_sme() -> (ApSme<FakeTokens>, MlmeStream, UserStream<FakeTokens>, TimeStream) {
        ApSme::new(DeviceInfo {
            addr: AP_ADDR,
            bands: vec![],
        })
    }

    struct FakeTokens;
    impl Tokens for FakeTokens {
        type StartToken = i32;
        type StopToken = i32;
    }
}