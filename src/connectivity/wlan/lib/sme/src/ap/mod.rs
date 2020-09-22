// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aid;
mod authenticator;
mod event;
mod remote_client;
mod rsn;
#[cfg(test)]
pub mod test_utils;

use event::*;
use remote_client::*;
use rsn::*;

use {
    crate::{
        phy_selection::{derive_phy_cbw_for_ap, get_device_band_info},
        responder::Responder,
        sink::MlmeSink,
        timer::{self, EventId, TimedEvent, Timer},
        MacAddr, MlmeRequest, Ssid,
    },
    anyhow::format_err,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo, MlmeEvent},
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::channel::{mpsc, oneshot},
    log::{debug, error, info, warn},
    std::collections::HashMap,
    wlan_common::{
        channel::{Channel, Phy},
        ie::{rsn::rsne::Rsne, SupportedRate},
        mac, RadioConfig,
    },
    wlan_rsn::{self, psk},
};

const DEFAULT_BEACON_PERIOD: u16 = 100;
const DEFAULT_DTIM_PERIOD: u8 = 1;

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub ssid: Ssid,
    pub password: Vec<u8>,
    pub radio_cfg: RadioConfig,
}

// OpRadioConfig keeps admitted configuration and operation state
#[derive(Clone, Debug, PartialEq)]
pub struct OpRadioConfig {
    phy: Phy,
    chan: Channel,
}

pub type TimeStream = timer::TimeStream<Event>;

enum State {
    Idle {
        ctx: Context,
    },
    Starting {
        ctx: Context,
        ssid: Ssid,
        rsn_cfg: Option<RsnCfg>,
        capabilities: mac::CapabilityInfo,
        rates: Vec<SupportedRate>,
        start_responder: Responder<StartResult>,
        stop_responders: Vec<Responder<fidl_sme::StopApResultCode>>,
        start_timeout: EventId,
        op_radio_cfg: OpRadioConfig,
    },
    Stopping {
        ctx: Context,
        stop_req: fidl_mlme::StopRequest,
        responders: Vec<Responder<fidl_sme::StopApResultCode>>,
        stop_timeout: Option<EventId>,
    },
    Started {
        bss: InfraBss,
    },
}

#[derive(Clone)]
pub struct RsnCfg {
    psk: psk::Psk,
    rsne: Rsne,
}

struct InfraBss {
    ssid: Ssid,
    rsn_cfg: Option<RsnCfg>,
    capabilities: mac::CapabilityInfo,
    rates: Vec<SupportedRate>,
    clients: HashMap<MacAddr, RemoteClient>,
    aid_map: aid::Map,
    op_radio_cfg: OpRadioConfig,
    ctx: Context,
}

pub struct Context {
    device_info: DeviceInfo,
    mlme_sink: MlmeSink,
    timer: Timer<Event>,
}

pub struct ApSme {
    state: Option<State>,
}

#[derive(Debug, PartialEq)]
pub enum StartResult {
    Success,
    AlreadyStarted,
    InternalError,
    Canceled,
    TimedOut,
    PreviousStartInProgress,
    InvalidArguments,
    DfsUnsupported,
}

impl ApSme {
    pub fn new(device_info: DeviceInfo) -> (Self, crate::MlmeStream, TimeStream) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (timer, time_stream) = timer::create_timer();
        let sme = ApSme {
            state: Some(State::Idle {
                ctx: Context { device_info, mlme_sink: MlmeSink::new(mlme_sink), timer },
            }),
        };
        (sme, mlme_stream, time_stream)
    }

    pub fn on_start_command(&mut self, config: Config) -> oneshot::Receiver<StartResult> {
        let (responder, receiver) = Responder::new();
        self.state = self.state.take().map(|state| match state {
            State::Idle { mut ctx } => {
                if let Err(result) = validate_config(&config) {
                    responder.respond(result);
                    return State::Idle { ctx };
                }

                let op_result = adapt_operation(&ctx.device_info, &config.radio_cfg);
                let op = match op_result {
                    Err(e) => {
                        error!("error in adapting to device capabilities. operation not accepted: {:?}", e);
                        responder.respond(StartResult::InternalError);
                        return State::Idle { ctx };
                    },
                    Ok(o) => o
                };

                let rsn_cfg_result = create_rsn_cfg(&config.ssid[..], &config.password[..]);
                let rsn_cfg = match rsn_cfg_result {
                    Err(e) => {
                        error!("error configuring RSN: {}", e);
                        responder.respond(StartResult::InternalError);
                        return State::Idle { ctx };
                    },
                    Ok(rsn_cfg) => rsn_cfg
                };

                let band_cap = match get_device_band_info(&ctx.device_info, op.chan.primary) {
                    None => {
                        error!("band info for channel {} not found", op.chan);
                        responder.respond(StartResult::InternalError);
                        return State::Idle { ctx };
                    },
                    Some(band_cap) => band_cap
                };

                let capabilities = mac::CapabilityInfo(band_cap.cap)
                    // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the ESS subfield to 1 and the IBSS
                    // subfield to 0 within transmitted Beacon or Probe Response frames.
                    .with_ess(true)
                    .with_ibss(false)
                    // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the Privacy subfield to 1 within
                    // transmitted Beacon, Probe Response, (Re)Association Response frames if data
                    // confidentiality is required for all Data frames exchanged within the BSS.
                    .with_privacy(rsn_cfg.is_some());

                let req = create_start_request(
                    &op,
                    &config.ssid,
                    rsn_cfg.as_ref(),
                    capabilities,
                    &band_cap.rates,
                );

                let rates = band_cap.rates.iter().map(|r| SupportedRate(*r)).collect();

                ctx.mlme_sink.send(MlmeRequest::Start(req));
                let event = Event::Sme { event: SmeEvent::StartTimeout };
                let start_timeout = ctx.timer.schedule(event);

                State::Starting {
                    ctx,
                    ssid: config.ssid,
                    rsn_cfg,
                    capabilities,
                    rates,
                    start_responder: responder,
                    stop_responders: vec![],
                    start_timeout,
                    op_radio_cfg: op,
                }
            },
            s @ State::Starting { .. } => {
                responder.respond(StartResult::PreviousStartInProgress);
                s
            },
            s @ State::Stopping { .. } => {
                responder.respond(StartResult::Canceled);
                s
            },
            s @ State::Started { .. } => {
                responder.respond(StartResult::AlreadyStarted);
                s
            }
        });
        receiver
    }

    pub fn on_stop_command(&mut self) -> oneshot::Receiver<fidl_sme::StopApResultCode> {
        let (responder, receiver) = Responder::new();
        self.state = self.state.take().map(|mut state| match state {
            s @ State::Idle { .. } => {
                responder.respond(fidl_sme::StopApResultCode::Success);
                s
            }
            State::Starting { ref mut stop_responders, .. } => {
                stop_responders.push(responder);
                state
            }
            State::Stopping { mut ctx, stop_req, mut responders, mut stop_timeout } => {
                responders.push(responder);
                // No stop request is ongoing, so forward this stop request.
                // The previous stop request may have timed out or failed and we are in an
                // unclean state where we don't know whether the AP has stopped or not.
                if stop_timeout.is_none() {
                    stop_timeout.replace(send_stop_req(&mut ctx, stop_req.clone()));
                }
                State::Stopping { ctx, stop_req, responders, stop_timeout }
            }
            State::Started { mut bss } => {
                // IEEE Std 802.11-2016, 6.3.12.2.3: The SME should notify associated non-AP STAs of
                // imminent infrastructure BSS termination before issuing the MLME-STOP.request
                // primitive.
                for (client_addr, _) in &bss.clients {
                    bss.ctx.mlme_sink.send(MlmeRequest::Deauthenticate(
                        fidl_mlme::DeauthenticateRequest {
                            peer_sta_address: *client_addr,
                            // This seems to be the most appropriate reason code (IEEE Std
                            // 802.11-2016, Table 9-45): Requesting STA is leaving the BSS (or
                            // resetting). The spec doesn't seem to mandate a choice of reason code
                            // here, so Fuchsia picks STA_LEAVING.
                            reason_code: fidl_mlme::ReasonCode::StaLeaving,
                        },
                    ));
                }

                let stop_req = fidl_mlme::StopRequest { ssid: bss.ssid.clone() };
                let timeout = send_stop_req(&mut bss.ctx, stop_req.clone());
                State::Stopping {
                    ctx: bss.ctx,
                    stop_req,
                    responders: vec![responder],
                    stop_timeout: Some(timeout),
                }
            }
        });
        receiver
    }

    pub fn get_running_ap(&self) -> Option<fidl_sme::Ap> {
        match self.state.as_ref() {
            Some(State::Started { bss: InfraBss { ssid, op_radio_cfg, clients, .. }, .. }) => {
                Some(fidl_sme::Ap {
                    ssid: ssid.to_vec(),
                    channel: op_radio_cfg.chan.primary,
                    num_clients: clients.len() as u16,
                })
            }
            _ => None,
        }
    }
}

fn send_stop_req(ctx: &mut Context, stop_req: fidl_mlme::StopRequest) -> EventId {
    let event = Event::Sme { event: SmeEvent::StopTimeout };
    let stop_timeout = ctx.timer.schedule(event);
    ctx.mlme_sink.send(MlmeRequest::Stop(stop_req.clone()));
    stop_timeout
}

/// Adapt user-providing operation condition to underlying device capabilities.
fn adapt_operation(
    device_info: &DeviceInfo,
    usr_cfg: &RadioConfig,
) -> Result<OpRadioConfig, anyhow::Error> {
    // TODO(porce): .expect() may go way, if wlantool absorbs the default value,
    // eg. CBW20 in HT. But doing so would hinder later control from WLANCFG.
    if usr_cfg.phy.is_none() || usr_cfg.cbw.is_none() || usr_cfg.primary_chan.is_none() {
        return Err(format_err!("Incomplete user config: {:?}", usr_cfg));
    }

    let phy = usr_cfg.phy.unwrap();
    let chan = Channel::new(usr_cfg.primary_chan.unwrap(), usr_cfg.cbw.unwrap());
    if !chan.is_valid() {
        return Err(format_err!("Invalid channel: {:?}", usr_cfg));
    }

    let (phy_adapted, cbw_adapted) = derive_phy_cbw_for_ap(device_info, &phy, &chan);
    Ok(OpRadioConfig { phy: phy_adapted, chan: Channel::new(chan.primary, cbw_adapted) })
}

impl super::Station for ApSme {
    type Event = Event;

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", event);
        self.state = self.state.take().map(|state| match state {
            State::Idle { .. } => {
                warn!("received MlmeEvent while ApSme is idle {:?}", event);
                state
            }
            State::Starting {
                ctx,
                ssid,
                rsn_cfg,
                capabilities,
                rates,
                start_responder,
                stop_responders,
                start_timeout,
                op_radio_cfg,
            } => match event {
                MlmeEvent::StartConf { resp } => handle_start_conf(
                    resp,
                    ctx,
                    ssid,
                    rsn_cfg,
                    capabilities,
                    rates,
                    op_radio_cfg,
                    start_responder,
                    stop_responders,
                ),
                _ => {
                    warn!("received MlmeEvent while ApSme is starting {:?}", event);
                    State::Starting {
                        ctx,
                        ssid,
                        rsn_cfg,
                        capabilities,
                        rates,
                        start_responder,
                        stop_responders,
                        start_timeout,
                        op_radio_cfg,
                    }
                }
            },
            State::Stopping { ctx, stop_req, mut responders, mut stop_timeout } => match event {
                MlmeEvent::StopConf { resp } => match resp.result_code {
                    fidl_mlme::StopResultCodes::Success
                    | fidl_mlme::StopResultCodes::BssAlreadyStopped => {
                        for responder in responders.drain(..) {
                            responder.respond(fidl_sme::StopApResultCode::Success);
                        }
                        State::Idle { ctx }
                    }
                    fidl_mlme::StopResultCodes::InternalError => {
                        for responder in responders.drain(..) {
                            responder.respond(fidl_sme::StopApResultCode::InternalError);
                        }
                        stop_timeout.take();
                        State::Stopping { ctx, stop_req, responders, stop_timeout }
                    }
                },
                _ => {
                    warn!("received MlmeEvent while ApSme is stopping {:?}", event);
                    State::Stopping { ctx, stop_req, responders, stop_timeout }
                }
            },
            State::Started { mut bss } => {
                match event {
                    MlmeEvent::OnChannelSwitched { info } => bss.handle_channel_switch(info),
                    MlmeEvent::AuthenticateInd { ind } => bss.handle_auth_ind(ind),
                    MlmeEvent::DeauthenticateInd { ind } => {
                        bss.handle_deauth(&ind.peer_sta_address)
                    }
                    // TODO(37891): This path should never be taken, as the MLME will never send
                    // this. Make sure this is the case.
                    MlmeEvent::DeauthenticateConf { resp } => {
                        bss.handle_deauth(&resp.peer_sta_address)
                    }
                    MlmeEvent::AssociateInd { ind } => bss.handle_assoc_ind(ind),
                    MlmeEvent::DisassociateInd { ind } => bss.handle_disassoc_ind(ind),
                    MlmeEvent::EapolInd { ind } => bss.handle_eapol_ind(ind),
                    MlmeEvent::EapolConf { resp } => {
                        if resp.result_code != fidl_mlme::EapolResultCodes::Success {
                            // TODO(fxbug.dev/29301) - Handle unsuccessful EAPoL confirmation. It doesn't
                            //                  include client address, though. Maybe we can just
                            //                  ignore these messages and just set a handshake
                            //                  timeout instead
                            info!("Received unsuccessful EapolConf");
                        }
                    }
                    _ => warn!("unsupported MlmeEvent type {:?}; ignoring", event),
                }
                State::Started { bss }
            }
        });
    }

    fn on_timeout(&mut self, timed_event: TimedEvent<Event>) {
        self.state = self.state.take().map(|mut state| match state {
            State::Idle { .. } => state,
            State::Starting {
                start_timeout,
                mut ctx,
                start_responder,
                stop_responders,
                capabilities,
                rates,
                ssid,
                rsn_cfg,
                op_radio_cfg,
            } => match timed_event.event {
                Event::Sme { event } => match event {
                    SmeEvent::StartTimeout if start_timeout == timed_event.id => {
                        warn!("Timed out waiting for MLME to start");
                        start_responder.respond(StartResult::TimedOut);
                        if stop_responders.is_empty() {
                            State::Idle { ctx }
                        } else {
                            let stop_req = fidl_mlme::StopRequest { ssid };
                            let timeout = send_stop_req(&mut ctx, stop_req.clone());
                            State::Stopping {
                                ctx,
                                stop_req,
                                responders: stop_responders,
                                stop_timeout: Some(timeout),
                            }
                        }
                    }
                    _ => State::Starting {
                        start_timeout,
                        ctx,
                        start_responder,
                        stop_responders,
                        capabilities,
                        rates,
                        ssid,
                        rsn_cfg,
                        op_radio_cfg,
                    },
                },
                _ => State::Starting {
                    start_timeout,
                    ctx,
                    start_responder,
                    stop_responders,
                    capabilities,
                    rates,
                    ssid,
                    rsn_cfg,
                    op_radio_cfg,
                },
            },
            State::Stopping { ctx, stop_req, mut responders, mut stop_timeout } => {
                match timed_event.event {
                    Event::Sme { event } => match event {
                        SmeEvent::StopTimeout if stop_timeout.is_some() => {
                            if stop_timeout == Some(timed_event.id) {
                                for responder in responders.drain(..) {
                                    responder.respond(fidl_sme::StopApResultCode::TimedOut);
                                }
                                stop_timeout.take();
                            }
                        }
                        _ => (),
                    },
                    _ => (),
                }
                // If timeout triggered, then the responders and the timeout are cleared, and
                // we are left in an unclean stopping state
                State::Stopping { ctx, stop_req, responders, stop_timeout }
            }
            State::Started { ref mut bss } => {
                bss.handle_timeout(timed_event);
                state
            }
        });
    }
}

fn validate_config(config: &Config) -> Result<(), StartResult> {
    let rc = &config.radio_cfg;
    if rc.phy.is_none() || rc.cbw.is_none() || rc.primary_chan.is_none() {
        return Err(StartResult::InvalidArguments);
    }
    let c = Channel::new(rc.primary_chan.unwrap(), config.radio_cfg.cbw.unwrap());
    if !c.is_valid() {
        Err(StartResult::InvalidArguments)
    } else if c.is_dfs() {
        Err(StartResult::DfsUnsupported)
    } else {
        Ok(())
    }
}

fn handle_start_conf(
    conf: fidl_mlme::StartConfirm,
    mut ctx: Context,
    ssid: Ssid,
    rsn_cfg: Option<RsnCfg>,
    capabilities: mac::CapabilityInfo,
    rates: Vec<SupportedRate>,
    op_radio_cfg: OpRadioConfig,
    start_responder: Responder<StartResult>,
    stop_responders: Vec<Responder<fidl_sme::StopApResultCode>>,
) -> State {
    if stop_responders.is_empty() {
        match conf.result_code {
            fidl_mlme::StartResultCodes::Success => {
                start_responder.respond(StartResult::Success);
                State::Started {
                    bss: InfraBss {
                        ssid,
                        rsn_cfg,
                        clients: HashMap::new(),
                        aid_map: aid::Map::default(),
                        capabilities,
                        rates,
                        op_radio_cfg,
                        ctx,
                    },
                }
            }
            result_code => {
                error!("failed to start BSS: {:?}", result_code);
                start_responder.respond(StartResult::InternalError);
                State::Idle { ctx }
            }
        }
    } else {
        start_responder.respond(StartResult::Canceled);
        let stop_req = fidl_mlme::StopRequest { ssid };
        let timeout = send_stop_req(&mut ctx, stop_req.clone());
        State::Stopping { ctx, stop_req, responders: stop_responders, stop_timeout: Some(timeout) }
    }
}

impl InfraBss {
    /// Removes a client from the map.
    ///
    /// A client may only be removed via |remove_client| if:
    ///
    /// - MLME-DEAUTHENTICATE.request has been issued for the client, or,
    /// - MLME-DEAUTHENTICATE.indication or MLME-DEAUTHENTICATE.confirm has been received for the
    ///   client, or,
    /// - MLME-AUTHENTICATE.indication is being handled (see comment in |handle_auth_ind| for
    ///   details).
    ///
    /// If the client has an AID, its AID will be released from the AID map.
    ///
    /// Returns true if a client was removed, otherwise false.
    fn remove_client(&mut self, addr: &MacAddr) -> bool {
        if let Some(client) = self.clients.remove(addr) {
            if let Some(aid) = client.aid() {
                self.aid_map.release_aid(aid);
            }
            true
        } else {
            false
        }
    }

    fn handle_channel_switch(&mut self, info: fidl_mlme::ChannelSwitchInfo) {
        info!("Channel switch for AP {:?}", info);
        self.op_radio_cfg.chan.primary = info.new_channel;
    }

    fn handle_auth_ind(&mut self, ind: fidl_mlme::AuthenticateIndication) {
        let peer_addr = ind.peer_sta_address;
        if self.remove_client(&peer_addr) {
            // This may occur if an already authenticated client on the SME receives a fresh
            // MLME-AUTHENTICATE.indication from the MLME.
            //
            // This is safe, as we will make a fresh the client state and return an appropriate
            // MLME-AUTHENTICATE.response to the MLME, indicating whether it should deauthenticate
            // the client or not.
            warn!(
                "client {:02X?} is trying to reauthenticate; removing client and starting again",
                peer_addr
            );
        }
        let mut client = RemoteClient::new(peer_addr);
        client.handle_auth_ind(&mut self.ctx, ind.auth_type);
        if !client.authenticated() {
            info!("client {:02X?} was not authenticated", peer_addr);
            return;
        }

        info!("client {:02X?} authenticated", peer_addr);
        self.clients.insert(peer_addr, client);
    }

    fn handle_deauth(&mut self, peer_addr: &MacAddr) {
        if !self.remove_client(peer_addr) {
            warn!(
                "client {:02X?} never authenticated, ignoring deauthentication request",
                peer_addr
            );
            return;
        }

        info!("client {:02X?} deauthenticated", peer_addr);
    }

    fn handle_assoc_ind(&mut self, ind: fidl_mlme::AssociateIndication) {
        let peer_addr = ind.peer_sta_address;

        let client = match self.clients.get_mut(&peer_addr) {
            None => {
                warn!(
                    "client {:02X?} never authenticated, ignoring association indication",
                    peer_addr
                );
                return;
            }
            Some(client) => client,
        };

        client.handle_assoc_ind(
            &mut self.ctx,
            &mut self.aid_map,
            self.capabilities,
            ind.cap,
            &self.rates,
            &ind.rates,
            &self.rsn_cfg,
            ind.rsne,
        );
        if !client.authenticated() {
            warn!("client {:02X?} failed to associate and was deauthenticated", peer_addr);
            self.remove_client(&peer_addr);
        } else if !client.associated() {
            warn!("client {:02X?} failed to associate but did not deauthenticate", peer_addr);
        } else {
            info!("client {:02X?} associated", peer_addr);
        }
    }

    fn handle_disassoc_ind(&mut self, ind: fidl_mlme::DisassociateIndication) {
        let peer_addr = ind.peer_sta_address;

        let client = match self.clients.get_mut(&peer_addr) {
            None => {
                warn!(
                    "client {:02X?} never authenticated, ignoring disassociation indication",
                    peer_addr
                );
                return;
            }
            Some(client) => client,
        };

        client.handle_disassoc_ind(&mut self.ctx, &mut self.aid_map);
        if client.associated() {
            panic!("client {:02X?} didn't disassociate? this should never happen!", peer_addr)
        } else {
            info!("client {:02X?} disassociated", peer_addr);
        }
    }

    fn handle_timeout(&mut self, timed_event: TimedEvent<Event>) {
        match timed_event.event {
            Event::Sme { .. } => (),
            Event::Client { addr, event } => {
                let client = match self.clients.get_mut(&addr) {
                    None => {
                        return;
                    }
                    Some(client) => client,
                };

                client.handle_timeout(&mut self.ctx, timed_event.id, event);
                if !client.authenticated() {
                    self.remove_client(&addr);
                    info!("client {:02X?} lost authentication", addr);
                }
            }
        }
    }

    fn handle_eapol_ind(&mut self, ind: fidl_mlme::EapolIndication) {
        let peer_addr = ind.src_addr;
        let client = match self.clients.get_mut(&peer_addr) {
            None => {
                warn!("client {:02X?} never authenticated, ignoring EAPoL indication", peer_addr);
                return;
            }
            Some(client) => client,
        };

        client.handle_eapol_ind(&mut self.ctx, &ind.data[..]);
    }
}

fn create_rsn_cfg(ssid: &[u8], password: &[u8]) -> Result<Option<RsnCfg>, anyhow::Error> {
    if password.is_empty() {
        Ok(None)
    } else {
        let psk = psk::compute(password, ssid)?;
        Ok(Some(RsnCfg { psk, rsne: create_wpa2_psk_rsne() }))
    }
}

fn create_start_request(
    op: &OpRadioConfig,
    ssid: &Ssid,
    ap_rsn: Option<&RsnCfg>,
    capabilities: mac::CapabilityInfo,
    rates: &[u8],
) -> fidl_mlme::StartRequest {
    let rsne_bytes = ap_rsn.as_ref().map(|RsnCfg { rsne, .. }| {
        let mut buf = Vec::with_capacity(rsne.len());
        if let Err(e) = rsne.write_into(&mut buf) {
            error!("error writing RSNE into MLME-START.request: {}", e);
        }
        buf
    });

    let (cbw, _secondary80) = op.chan.cbw.to_fidl();

    fidl_mlme::StartRequest {
        ssid: ssid.clone(),
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: op.chan.primary,
        cap: capabilities.raw(),
        rates: rates.to_vec(),
        country: fidl_mlme::Country {
            // TODO(fxbug.dev/29490): Get config from wlancfg
            alpha2: ['U' as u8, 'S' as u8],
            suffix: fidl_mlme::COUNTRY_ENVIRON_ALL,
        },
        rsne: rsne_bytes,
        mesh_id: vec![],
        phy: op.phy.to_fidl(),
        cbw: cbw,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{test_utils::*, MlmeStream, Station},
        fidl_fuchsia_wlan_mlme as fidl_mlme,
        wlan_common::{
            assert_variant,
            channel::{Cbw, Phy},
            ie::*,
            mac::Aid,
            RadioConfig,
        },
    };

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
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0x11, 0x00, 0x0f, 0xac, 0x04, // group management cipher suite -- CCMP-128
    ];

    fn radio_cfg(pri_chan: u8) -> RadioConfig {
        RadioConfig::new(Phy::Ht, Cbw::Cbw20, pri_chan)
    }

    fn unprotected_config() -> Config {
        Config { ssid: SSID.to_vec(), password: vec![], radio_cfg: radio_cfg(11) }
    }

    fn protected_config() -> Config {
        Config {
            ssid: SSID.to_vec(),
            password: vec![0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68],
            radio_cfg: radio_cfg(11),
        }
    }

    fn create_channel_switch_ind(channel: u8) -> MlmeEvent {
        MlmeEvent::OnChannelSwitched { info: fidl_mlme::ChannelSwitchInfo { new_channel: channel } }
    }

    #[test]
    fn test_validate_config() {
        assert_eq!(
            Err(StartResult::InvalidArguments),
            validate_config(&Config { ssid: vec![], password: vec![], radio_cfg: radio_cfg(15) })
        );
        assert_eq!(
            Err(StartResult::DfsUnsupported),
            validate_config(&Config { ssid: vec![], password: vec![], radio_cfg: radio_cfg(52) })
        );
        assert_eq!(
            Ok(()),
            validate_config(&Config { ssid: vec![], password: vec![], radio_cfg: radio_cfg(40) })
        );
    }

    #[test]
    fn authenticate_while_sme_is_idle() {
        let (mut sme, mut mlme_stream, _) = create_sme();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        assert_variant!(mlme_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty");
        });
    }

    // Check status when sme is idle
    #[test]
    fn status_when_sme_is_idle() {
        let (sme, _, _) = create_sme();
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_starts_success() {
        let (mut sme, mut mlme_stream, _) = create_sme();
        let mut receiver = sme.on_start_command(unprotected_config());

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(start_req))) => {
            assert_eq!(start_req.ssid, SSID.to_vec());
            assert_eq!(
                start_req.cap,
                mac::CapabilityInfo(0).with_short_preamble(true).with_ess(true).raw(),
            );
            assert_eq!(start_req.bss_type, fidl_mlme::BssTypes::Infrastructure);
            assert_ne!(start_req.beacon_period, 0);
            assert_ne!(start_req.dtim_period, 0);
            assert_eq!(
                start_req.channel,
                unprotected_config().radio_cfg.primary_chan.expect("invalid config")
            );
            assert!(start_req.rsne.is_none());
        });

        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
    }

    // Check status when Ap starting and started
    #[test]
    fn ap_starts_success_get_running_ap() {
        let (mut sme, mut mlme_stream, _) = create_sme();
        let mut receiver = sme.on_start_command(unprotected_config());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(_start_req))) => {});
        // status should be Starting
        assert_eq!(None, sme.get_running_ap());
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.primary_chan.unwrap(),
                num_clients: 0,
            }),
            sme.get_running_ap()
        );
    }

    // Check status after channel change
    #[test]
    fn ap_check_status_after_channel_change() {
        let (mut sme, _, _) = start_unprotected_ap();
        // Check status
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.primary_chan.unwrap(),
                num_clients: 0,
            }),
            sme.get_running_ap()
        );
        sme.on_mlme_event(create_channel_switch_ind(6));
        // Check status
        assert_eq!(
            Some(fidl_sme::Ap { ssid: SSID.to_vec(), channel: 6, num_clients: 0 }),
            sme.get_running_ap()
        );
    }

    #[test]
    fn ap_starts_timeout() {
        let (mut sme, _, mut time_stream) = create_sme();
        let mut receiver = sme.on_start_command(unprotected_config());

        let (_, event) = time_stream.try_next().unwrap().expect("expect timer message");
        sme.on_timeout(event);

        assert_eq!(Ok(Some(StartResult::TimedOut)), receiver.try_recv());
        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_starts_fails() {
        let (mut sme, _, _) = create_sme();
        let mut receiver = sme.on_start_command(unprotected_config());

        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::NotSupported));
        assert_eq!(Ok(Some(StartResult::InternalError)), receiver.try_recv());
        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn start_req_while_ap_is_starting() {
        let (mut sme, _, _) = create_sme();
        let mut receiver_one = sme.on_start_command(unprotected_config());

        // While SME is starting, any start request receives an error immediately
        let mut receiver_two = sme.on_start_command(unprotected_config());
        assert_eq!(Ok(Some(StartResult::PreviousStartInProgress)), receiver_two.try_recv());

        // Start confirmation for first request should still have an affect
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver_one.try_recv());
    }

    #[test]
    fn start_req_while_ap_is_stopping() {
        let (mut sme, _, _) = start_unprotected_ap();
        let mut stop_receiver = sme.on_stop_command();
        let mut start_receiver = sme.on_start_command(unprotected_config());
        assert_eq!(Ok(None), stop_receiver.try_recv());
        assert_eq!(Ok(Some(StartResult::Canceled)), start_receiver.try_recv());
    }

    #[test]
    fn ap_stops_while_idle() {
        let (mut sme, _, _) = create_sme();
        let mut receiver = sme.on_stop_command();
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());
    }

    #[test]
    fn stop_req_while_ap_is_starting_then_succeeds() {
        let (mut sme, mut mlme_stream, _) = create_sme();
        let mut start_receiver = sme.on_start_command(unprotected_config());
        let mut stop_receiver = sme.on_stop_command();
        assert_eq!(Ok(None), start_receiver.try_recv());
        assert_eq!(Ok(None), stop_receiver.try_recv());

        // Verify start request is sent to MLME but not stop request yet
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(_))));
        assert_variant!(mlme_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty");
        });

        // Once start confirmation is finished, then stop request is sent out
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::Success));
        assert_eq!(Ok(Some(StartResult::Canceled)), start_receiver.try_recv());
        assert_eq!(Ok(None), stop_receiver.try_recv());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        // Respond with a successful stop result code
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver.try_recv());
    }

    #[test]
    fn stop_req_while_ap_is_starting_then_times_out() {
        let (mut sme, mut mlme_stream, mut time_stream) = create_sme();
        let mut start_receiver = sme.on_start_command(unprotected_config());
        let mut stop_receiver = sme.on_stop_command();
        assert_eq!(Ok(None), start_receiver.try_recv());
        assert_eq!(Ok(None), stop_receiver.try_recv());

        // Verify start request is sent to MLME but not stop request yet
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(_))));
        assert_variant!(mlme_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty");
        });

        // Time out the start request. Then stop request is sent out
        let (_, event) = time_stream.try_next().unwrap().expect("expect timer message");
        sme.on_timeout(event);
        assert_eq!(Ok(Some(StartResult::TimedOut)), start_receiver.try_recv());
        assert_eq!(Ok(None), stop_receiver.try_recv());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        // Respond with a successful stop result code
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver.try_recv());
    }

    #[test]
    fn ap_stops_after_started() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let mut receiver = sme.on_stop_command();

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::BssAlreadyStopped));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());
    }

    #[test]
    fn ap_stops_after_started_and_deauths_all_clients() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        // Check status
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.primary_chan.unwrap(),
                num_clients: 1,
            }),
            sme.get_running_ap()
        );
        let mut receiver = sme.on_stop_command();
        assert_variant!(
        mlme_stream.try_next(),
        Ok(Some(MlmeRequest::Deauthenticate(deauth_req))) => {
            assert_eq!(deauth_req.peer_sta_address, client.addr);
            assert_eq!(deauth_req.reason_code, fidl_mlme::ReasonCode::StaLeaving);
        });

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());

        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_queues_concurrent_stop_requests() {
        let (mut sme, _, _) = start_unprotected_ap();
        let mut receiver1 = sme.on_stop_command();
        let mut receiver2 = sme.on_stop_command();

        assert_eq!(Ok(None), receiver1.try_recv());
        assert_eq!(Ok(None), receiver2.try_recv());

        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver1.try_recv());
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver2.try_recv());
    }

    #[test]
    fn uncleaned_stopping_state() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let mut stop_receiver1 = sme.on_stop_command();
        // Clear out the stop request
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        assert_eq!(Ok(None), stop_receiver1.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::InternalError));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::InternalError)), stop_receiver1.try_recv());

        // While in unclean stopping state, no start request can be made
        let mut start_receiver = sme.on_start_command(unprotected_config());
        assert_eq!(Ok(Some(StartResult::Canceled)), start_receiver.try_recv());
        assert_variant!(mlme_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty");
        });

        // SME will forward another stop request to lower layer
        let mut stop_receiver2 = sme.on_stop_command();
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        // Respond successful this time
        assert_eq!(Ok(None), stop_receiver2.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCodes::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver2.try_recv());
    }

    #[test]
    fn client_authenticates_supported_authentication_type() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);
    }

    #[test]
    fn client_authenticates_unsupported_authentication_type() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let client = Client::default();
        let auth_ind = client.create_auth_ind(fidl_mlme::AuthenticationTypes::FastBssTransition);
        sme.on_mlme_event(auth_ind);
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Refused);
    }

    #[test]
    fn client_associates_unprotected_network() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCodes::Success,
            false,
        );
    }

    #[test]
    fn client_associates_valid_rsne() {
        let (mut sme, mut mlme_stream, _) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCodes::Success,
            true,
        );
        client.verify_eapol_req(&mut mlme_stream);
    }

    #[test]
    fn client_associates_invalid_rsne() {
        let (mut sme, mut mlme_stream, _) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_refused_assoc_resp(
            &mut mlme_stream,
            fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch,
        );
    }

    #[test]
    fn rsn_handshake_timeout() {
        let (mut sme, mut mlme_stream, mut time_stream) = start_protected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        // Drain the association timeout message.
        assert_variant!(time_stream.try_next(), Ok(Some(_)));

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCodes::Success,
            true,
        );

        // Drain the RSNA negotiation timeout message.
        assert_variant!(time_stream.try_next(), Ok(Some(_)));

        for _i in 0..4 {
            client.verify_eapol_req(&mut mlme_stream);

            let (_, event) = time_stream.try_next().unwrap().expect("expect timer message");
            // Calling `on_timeout` with a different event ID is a no-op
            let mut fake_event = event.clone();
            fake_event.id += 1;
            sme.on_timeout(fake_event);
            assert_variant!(mlme_stream.try_next(), Err(e) => {
                assert_eq!(e.to_string(), "receiver channel is empty")
            });
            sme.on_timeout(event);
        }

        client.verify_deauth_req(&mut mlme_stream, fidl_mlme::ReasonCode::FourwayHandshakeTimeout);
    }

    #[test]
    fn client_restarts_authentication_flow() {
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap();
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);
        client.associate_and_drain_mlme(&mut sme, &mut mlme_stream, None);

        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCodes::Success,
            false,
        );
    }

    #[test]
    fn multiple_clients_associate() {
        let (mut sme, mut mlme_stream, _) = start_protected_ap();
        let client1 = Client::default();
        let client2 = Client { addr: CLIENT_ADDR2 };

        sme.on_mlme_event(client1.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client1.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client2.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client2.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCodes::Success);

        sme.on_mlme_event(client1.create_assoc_ind(Some(RSNE.to_vec())));
        client1.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCodes::Success,
            true,
        );
        client1.verify_eapol_req(&mut mlme_stream);

        sme.on_mlme_event(client2.create_assoc_ind(Some(RSNE.to_vec())));
        client2.verify_assoc_resp(
            &mut mlme_stream,
            2,
            fidl_mlme::AssociateResultCodes::Success,
            true,
        );
        client2.verify_eapol_req(&mut mlme_stream);
    }

    #[test]
    fn test_adapt_operation() {
        // Invalid input
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig { phy: None, cbw: Some(Cbw::Cbw20), primary_chan: Some(1) };
            let got = adapt_operation(&dinf, &ucfg);
            assert!(got.is_err());
        }
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw40, 48);
            let got = adapt_operation(&dinf, &ucfg);
            assert!(got.is_err());
        }

        // VHT device, VHT config
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw80, 48);
            let want = OpRadioConfig { phy: Phy::Vht, chan: Channel::new(48, Cbw::Cbw80) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Vht, chan: Channel::new(48, Cbw::Cbw40Below) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw20, 48);
            let want = OpRadioConfig { phy: Phy::Vht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }

        // VHT device, HT config
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw40Below) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_vht(ChanWidthSet::TWENTY_ONLY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }

        // HT device, VHT config
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw80, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw40Below) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw40Below) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Vht, Cbw::Cbw20, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }

        // HT device, HT config
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw40Below) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_FORTY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
        {
            let dinf = fake_device_info_ht(ChanWidthSet::TWENTY_ONLY);
            let ucfg = RadioConfig::new(Phy::Ht, Cbw::Cbw40Below, 48);
            let want = OpRadioConfig { phy: Phy::Ht, chan: Channel::new(48, Cbw::Cbw20) };
            let got = adapt_operation(&dinf, &ucfg).unwrap();
            assert_eq!(want, got);
        }
    }

    fn create_start_conf(result_code: fidl_mlme::StartResultCodes) -> MlmeEvent {
        MlmeEvent::StartConf { resp: fidl_mlme::StartConfirm { result_code } }
    }

    fn create_stop_conf(result_code: fidl_mlme::StopResultCodes) -> MlmeEvent {
        MlmeEvent::StopConf { resp: fidl_mlme::StopConfirm { result_code } }
    }

    struct Client {
        addr: MacAddr,
    }

    impl Client {
        fn default() -> Self {
            Client { addr: CLIENT_ADDR }
        }

        fn authenticate_and_drain_mlme(
            &self,
            sme: &mut ApSme,
            mlme_stream: &mut crate::MlmeStream,
        ) {
            sme.on_mlme_event(self.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
            assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::AuthResponse(..))));
        }

        fn associate_and_drain_mlme(
            &self,
            sme: &mut ApSme,
            mlme_stream: &mut crate::MlmeStream,
            rsne: Option<Vec<u8>>,
        ) {
            sme.on_mlme_event(self.create_assoc_ind(rsne));
            assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::AssocResponse(..))));
        }

        fn create_auth_ind(&self, auth_type: fidl_mlme::AuthenticationTypes) -> MlmeEvent {
            MlmeEvent::AuthenticateInd {
                ind: fidl_mlme::AuthenticateIndication { peer_sta_address: self.addr, auth_type },
            }
        }

        fn create_assoc_ind(&self, rsne: Option<Vec<u8>>) -> MlmeEvent {
            MlmeEvent::AssociateInd {
                ind: fidl_mlme::AssociateIndication {
                    peer_sta_address: self.addr,
                    listen_interval: 100,
                    ssid: Some(SSID.to_vec()),
                    rsne,
                    cap: mac::CapabilityInfo(0).with_short_preamble(true).raw(),
                    rates: vec![
                        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
                    ],
                },
            }
        }

        fn verify_auth_resp(
            &self,
            mlme_stream: &mut MlmeStream,
            result_code: fidl_mlme::AuthenticateResultCodes,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::AuthResponse(auth_resp))) => {
                assert_eq!(auth_resp.peer_sta_address, self.addr);
                assert_eq!(auth_resp.result_code, result_code);
            });
        }

        fn verify_assoc_resp(
            &self,
            mlme_stream: &mut MlmeStream,
            aid: Aid,
            result_code: fidl_mlme::AssociateResultCodes,
            privacy: bool,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::AssocResponse(assoc_resp))) => {
                assert_eq!(assoc_resp.peer_sta_address, self.addr);
                assert_eq!(assoc_resp.association_id, aid);
                assert_eq!(assoc_resp.result_code, result_code);
                assert_eq!(
                    assoc_resp.cap,
                    mac::CapabilityInfo(0).with_short_preamble(true).with_privacy(privacy).raw(),
                );
            });
        }

        fn verify_refused_assoc_resp(
            &self,
            mlme_stream: &mut MlmeStream,
            result_code: fidl_mlme::AssociateResultCodes,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::AssocResponse(assoc_resp))) => {
                assert_eq!(assoc_resp.peer_sta_address, self.addr);
                assert_eq!(assoc_resp.association_id, 0);
                assert_eq!(assoc_resp.result_code, result_code);
                assert_eq!(assoc_resp.cap, 0);
            });
        }

        fn verify_eapol_req(&self, mlme_stream: &mut MlmeStream) {
            assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Eapol(eapol_req))) => {
                assert_eq!(eapol_req.src_addr, AP_ADDR);
                assert_eq!(eapol_req.dst_addr, self.addr);
                assert!(eapol_req.data.len() > 0);
            });
        }

        fn verify_deauth_req(
            &self,
            mlme_stream: &mut MlmeStream,
            reason_code: fidl_mlme::ReasonCode,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::Deauthenticate(deauth_req))) => {
                assert_eq!(deauth_req.peer_sta_address, self.addr);
                assert_eq!(deauth_req.reason_code, reason_code);
            });
        }
    }

    fn start_protected_ap() -> (ApSme, crate::MlmeStream, TimeStream) {
        start_ap(true)
    }

    fn start_unprotected_ap() -> (ApSme, crate::MlmeStream, TimeStream) {
        start_ap(false)
    }

    fn start_ap(protected: bool) -> (ApSme, crate::MlmeStream, TimeStream) {
        let (mut sme, mut mlme_stream, mut time_stream) = create_sme();
        let config = if protected { protected_config() } else { unprotected_config() };
        let mut receiver = sme.on_start_command(config);
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(..))));
        // drain time stream
        while let Ok(..) = time_stream.try_next() {}
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCodes::Success));

        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
        (sme, mlme_stream, time_stream)
    }

    fn create_sme() -> (ApSme, MlmeStream, TimeStream) {
        let mut device_info = fake_device_info(AP_ADDR);
        device_info.bands = vec![fake_2ghz_band_capabilities()];
        ApSme::new(device_info)
    }
}
