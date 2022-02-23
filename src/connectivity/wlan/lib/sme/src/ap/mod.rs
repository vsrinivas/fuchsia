// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aid;
mod authenticator;
mod event;
mod remote_client;
#[cfg(test)]
pub mod test_utils;

use event::*;
use remote_client::*;

use {
    crate::{mlme_event_name, responder::Responder, MlmeRequest, MlmeSink},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo, MlmeEvent},
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::channel::{mpsc, oneshot},
    ieee80211::{MacAddr, Ssid},
    log::{debug, error, info, warn},
    std::collections::HashMap,
    wlan_common::{
        capabilities::get_device_band_cap,
        channel::{Cbw, Channel},
        ie::{parse_ht_capabilities, rsn::rsne::Rsne, ChanWidthSet, SupportedRate},
        mac,
        timer::{self, EventId, TimedEvent, Timer},
        RadioConfig,
    },
    wlan_rsn::{self, psk},
};

const DEFAULT_BEACON_PERIOD: u16 = 100;
const DEFAULT_DTIM_PERIOD: u8 = 2;

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub ssid: Ssid,
    pub password: Vec<u8>,
    pub radio_cfg: RadioConfig,
}

// OpRadioConfig keeps admitted configuration and operation state
#[derive(Clone, Debug, PartialEq)]
pub struct OpRadioConfig {
    phy: fidl_common::WlanPhyType,
    channel: Channel,
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
    Canceled,
    TimedOut,
    InvalidArguments(String),
    PreviousStartInProgress,
    AlreadyStarted,
    InternalError,
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
                let band_cap =
                    match get_device_band_cap(&ctx.device_info, config.radio_cfg.channel.primary) {
                        None => {
                            responder.respond(StartResult::InvalidArguments(format!(
                                "Device has not band capabilities for channel {}",
                                config.radio_cfg.channel.primary,
                            )));
                            return State::Idle { ctx };
                        }
                        Some(bc) => bc,
                    };

                let op_radio_cfg = match validate_radio_cfg(&band_cap, &config.radio_cfg) {
                    Err(result) => {
                        responder.respond(result);
                        return State::Idle { ctx };
                    }
                    Ok(op_radio_cfg) => op_radio_cfg,
                };

                let rsn_cfg_result = create_rsn_cfg(&config.ssid, &config.password[..]);
                let rsn_cfg = match rsn_cfg_result {
                    Err(e) => {
                        responder.respond(e);
                        return State::Idle { ctx };
                    }
                    Ok(rsn_cfg) => rsn_cfg,
                };

                let capabilities =
                    mac::CapabilityInfo(ctx.device_info.softmac_hardware_capability as u16)
                        // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the ESS subfield to 1 and the IBSS
                        // subfield to 0 within transmitted Beacon or Probe Response frames.
                        .with_ess(true)
                        .with_ibss(false)
                        // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the Privacy subfield to 1 within
                        // transmitted Beacon, Probe Response, (Re)Association Response frames if data
                        // confidentiality is required for all Data frames exchanged within the BSS.
                        .with_privacy(rsn_cfg.is_some());

                let req = match create_start_request(
                    &op_radio_cfg,
                    &config.ssid,
                    rsn_cfg.as_ref(),
                    capabilities,
                    // The max length of fuchsia.wlan.mlme/BandCapability.basic_rates is
                    // less than fuchsia.wlan.mlme/StartRequest.rates.
                    &band_cap.basic_rates,
                ) {
                    Ok(req) => req,
                    Err(result) => {
                        responder.respond(result);
                        return State::Idle { ctx };
                    }
                };

                // TODO(fxbug.dev/28891): Select which rates are mandatory here.
                let rates = band_cap.basic_rates.iter().map(|r| SupportedRate(*r)).collect();

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
                    op_radio_cfg,
                }
            }
            s @ State::Starting { .. } => {
                responder.respond(StartResult::PreviousStartInProgress);
                s
            }
            s @ State::Stopping { .. } => {
                responder.respond(StartResult::Canceled);
                s
            }
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
            State::Idle { mut ctx } => {
                // We don't have an SSID, so just do a best-effort StopAP request with no SSID
                // filled in
                let stop_req = fidl_mlme::StopRequest { ssid: Ssid::empty().into() };
                let timeout = send_stop_req(&mut ctx, stop_req.clone());
                State::Stopping {
                    ctx,
                    stop_req,
                    responders: vec![responder],
                    stop_timeout: Some(timeout),
                }
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
                            reason_code: fidl_ieee80211::ReasonCode::StaLeaving,
                        },
                    ));
                }

                let stop_req = fidl_mlme::StopRequest { ssid: bss.ssid.to_vec() };
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
                    channel: op_radio_cfg.channel.primary,
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

impl super::Station for ApSme {
    type Event = Event;

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", &event);
        self.state = self.state.take().map(|state| match state {
            State::Idle { .. } => {
                warn!("received MlmeEvent while ApSme is idle {:?}", mlme_event_name(&event));
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
                    warn!(
                        "received MlmeEvent while ApSme is starting {:?}",
                        mlme_event_name(&event)
                    );
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
                    fidl_mlme::StopResultCode::Success
                    | fidl_mlme::StopResultCode::BssAlreadyStopped => {
                        for responder in responders.drain(..) {
                            responder.respond(fidl_sme::StopApResultCode::Success);
                        }
                        State::Idle { ctx }
                    }
                    fidl_mlme::StopResultCode::InternalError => {
                        for responder in responders.drain(..) {
                            responder.respond(fidl_sme::StopApResultCode::InternalError);
                        }
                        stop_timeout.take();
                        State::Stopping { ctx, stop_req, responders, stop_timeout }
                    }
                },
                _ => {
                    warn!(
                        "received MlmeEvent while ApSme is stopping {:?}",
                        mlme_event_name(&event)
                    );
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
                    // TODO(fxbug.dev/37891): This path should never be taken, as the MLME will never send
                    // this. Make sure this is the case.
                    MlmeEvent::DeauthenticateConf { resp } => {
                        bss.handle_deauth(&resp.peer_sta_address)
                    }
                    MlmeEvent::AssociateInd { ind } => bss.handle_assoc_ind(ind),
                    MlmeEvent::DisassociateInd { ind } => bss.handle_disassoc_ind(ind),
                    MlmeEvent::EapolInd { ind } => bss.handle_eapol_ind(ind),
                    MlmeEvent::EapolConf { resp } => bss.handle_eapol_conf(resp),
                    _ => {
                        warn!("unsupported MlmeEvent type {:?}; ignoring", mlme_event_name(&event))
                    }
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
                            let stop_req = fidl_mlme::StopRequest { ssid: ssid.to_vec() };
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

/// Validate the channel, PHY type, bandwidth, and band capabilities, in that order.
fn validate_radio_cfg(
    band_cap: &fidl_mlme::BandCapability,
    radio_cfg: &RadioConfig,
) -> Result<OpRadioConfig, StartResult> {
    let channel = radio_cfg.channel;
    // TODO(fxbug.dev/93171): We shouldn't expect to only start an AP in the US. The regulatory
    // enforcement for the channel should apply at a lower layer.
    if !channel.is_valid_in_us() {
        return Err(StartResult::InvalidArguments(format!("Invalid US channel {}", channel)));
    }
    if channel.is_dfs() {
        return Err(StartResult::InvalidArguments(format!(
            "DFS channels not supported: {}",
            channel
        )));
    }

    let phy = radio_cfg.phy;
    match phy {
        fidl_common::WlanPhyType::Dsss
        | fidl_common::WlanPhyType::Hr
        | fidl_common::WlanPhyType::Ofdm
        | fidl_common::WlanPhyType::Erp => match channel.cbw {
            Cbw::Cbw20 => (),
            _ => {
                return Err(StartResult::InvalidArguments(format!(
                    "PHY type {:?} not supported on channel {}",
                    phy, channel
                )))
            }
        },
        fidl_common::WlanPhyType::Ht => {
            match channel.cbw {
                Cbw::Cbw20 | Cbw::Cbw40 | Cbw::Cbw40Below => (),
                _ => {
                    return Err(StartResult::InvalidArguments(format!(
                        "HT-mode not supported for channel {}",
                        channel
                    )))
                }
            }

            match band_cap.ht_cap.as_ref() {
                None => {
                    return Err(StartResult::InvalidArguments(format!(
                        "No HT capabilities: {}",
                        channel
                    )))
                }
                Some(ht_cap) => {
                    let ht_cap = parse_ht_capabilities(&ht_cap.bytes[..]).map_err(|e| {
                        error!("failed to parse HT capability bytes: {:?}", e);
                        StartResult::InternalError
                    })?;
                    let ht_cap_info = ht_cap.ht_cap_info;
                    if ht_cap_info.chan_width_set() == ChanWidthSet::TWENTY_ONLY {
                        if channel.cbw != Cbw::Cbw20 {
                            return Err(StartResult::InvalidArguments(format!(
                                "20 MHz band capabilities does not support channel {}",
                                channel
                            )));
                        }
                    }
                }
            }
        }
        fidl_common::WlanPhyType::Vht => {
            match channel.cbw {
                Cbw::Cbw160 | Cbw::Cbw80P80 { .. } => {
                    return Err(StartResult::InvalidArguments(format!(
                        "Supported for channel {} in VHT mode not available",
                        channel
                    )))
                }
                _ => (),
            }

            if !channel.is_5ghz() {
                return Err(StartResult::InvalidArguments(format!(
                    "VHT only supported on 5 GHz channels: {}",
                    channel
                )));
            }

            if band_cap.vht_cap.is_none() {
                return Err(StartResult::InvalidArguments(format!(
                    "No VHT capabilities: {}",
                    channel
                )));
            }
        }
        fidl_common::WlanPhyType::Dmg
        | fidl_common::WlanPhyType::Tvht
        | fidl_common::WlanPhyType::S1G
        | fidl_common::WlanPhyType::Cdmg
        | fidl_common::WlanPhyType::Cmmg
        | fidl_common::WlanPhyType::He => {
            return Err(StartResult::InvalidArguments(format!("Unsupported PHY type: {:?}", phy)))
        }
    }

    Ok(OpRadioConfig { phy, channel })
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
            fidl_mlme::StartResultCode::Success => {
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
        let stop_req = fidl_mlme::StopRequest { ssid: ssid.to_vec() };
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

    fn handle_channel_switch(&mut self, info: fidl_internal::ChannelSwitchInfo) {
        info!("Channel switch for AP {:?}", info);
        self.op_radio_cfg.channel.primary = info.new_channel;
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
            ind.capability_info,
            &self.rates,
            &ind.rates.into_iter().map(|r| SupportedRate(r)).collect::<Vec<_>>()[..],
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

    fn handle_eapol_conf(&mut self, resp: fidl_mlme::EapolConfirm) {
        let client = match self.clients.get_mut(&resp.dst_addr) {
            None => {
                warn!("never sent EAPOL frame to client {:02X?}, ignoring confirm", resp.dst_addr);
                return;
            }
            Some(client) => client,
        };

        client.handle_eapol_conf(&mut self.ctx, resp.result_code);
    }
}

fn create_rsn_cfg(ssid: &Ssid, password: &[u8]) -> Result<Option<RsnCfg>, StartResult> {
    if password.is_empty() {
        Ok(None)
    } else {
        let psk_result = psk::compute(password, ssid);
        let psk = match psk_result {
            Err(e) => {
                return Err(StartResult::InvalidArguments(e.to_string()));
            }
            Ok(o) => o,
        };

        // Note: TKIP is legacy and considered insecure. Only allow CCMP usage
        // for group and pairwise ciphers.
        Ok(Some(RsnCfg { psk, rsne: Rsne::wpa2_rsne() }))
    }
}

fn create_start_request(
    op_radio_cfg: &OpRadioConfig,
    ssid: &Ssid,
    ap_rsn: Option<&RsnCfg>,
    capabilities: mac::CapabilityInfo,
    basic_rates: &[u8],
) -> Result<fidl_mlme::StartRequest, StartResult> {
    let rsne_bytes = ap_rsn.as_ref().map(|RsnCfg { rsne, .. }| {
        let mut buf = Vec::with_capacity(rsne.len());
        if let Err(e) = rsne.write_into(&mut buf) {
            error!("error writing RSNE into MLME-START.request: {}", e);
        }
        buf
    });

    let (channel_bandwidth, _secondary80) = op_radio_cfg.channel.cbw.to_fidl();

    if basic_rates.len() > fidl_internal::MAX_ASSOC_BASIC_RATES as usize {
        error!(
            "Too many basic rates ({}). Max is {}.",
            basic_rates.len(),
            fidl_internal::MAX_ASSOC_BASIC_RATES
        );
        return Err(StartResult::InternalError);
    }

    Ok(fidl_mlme::StartRequest {
        ssid: ssid.to_vec(),
        bss_type: fidl_internal::BssType::Infrastructure,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: op_radio_cfg.channel.primary,
        capability_info: capabilities.raw(),
        rates: basic_rates.to_vec(),
        country: fidl_mlme::Country {
            // TODO(fxbug.dev/29490): Get config from wlancfg
            alpha2: ['U' as u8, 'S' as u8],
            suffix: fidl_mlme::COUNTRY_ENVIRON_ALL,
        },
        rsne: rsne_bytes,
        mesh_id: vec![],
        phy: op_radio_cfg.phy,
        channel_bandwidth,
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{test_utils::*, MlmeStream, Station},
        fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_async as fasync,
        ieee80211::MacAddr,
        lazy_static::lazy_static,
        std::convert::TryFrom,
        test_case::test_case,
        wlan_common::{
            assert_variant,
            channel::Cbw,
            mac::Aid,
            test_utils::fake_capabilities::{
                fake_2ghz_band_capability_vht, fake_5ghz_band_capability,
                fake_5ghz_band_capability_ht_cbw,
            },
            RadioConfig,
        },
    };

    const AP_ADDR: MacAddr = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
    const CLIENT_ADDR: MacAddr = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];
    const CLIENT_ADDR2: MacAddr = [0x22, 0x22, 0x22, 0x22, 0x22, 0x22];
    lazy_static! {
        static ref SSID: Ssid = Ssid::try_from([0x46, 0x55, 0x43, 0x48, 0x53, 0x49, 0x41]).unwrap();
    }
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

    fn radio_cfg(primary_channel: u8) -> RadioConfig {
        RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, primary_channel)
    }

    fn unprotected_config() -> Config {
        Config { ssid: SSID.clone(), password: vec![], radio_cfg: radio_cfg(11) }
    }

    fn protected_config() -> Config {
        Config {
            ssid: SSID.clone(),
            password: vec![0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68],
            radio_cfg: radio_cfg(11),
        }
    }

    fn create_channel_switch_ind(channel: u8) -> MlmeEvent {
        MlmeEvent::OnChannelSwitched {
            info: fidl_internal::ChannelSwitchInfo { new_channel: channel },
        }
    }

    #[test_case(false, None, fidl_common::WlanPhyType::Ht, 15, Cbw::Cbw20; "invalid US channel")]
    #[test_case(false, None, fidl_common::WlanPhyType::Ht, 52, Cbw::Cbw20; "DFS channel")]
    #[test_case(false, None, fidl_common::WlanPhyType::Dmg, 1, Cbw::Cbw20; "DMG not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Tvht, 1, Cbw::Cbw20; "TVHT not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::S1G, 1, Cbw::Cbw20; "S1G not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Cdmg, 1, Cbw::Cbw20; "CDMG not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Cmmg, 1, Cbw::Cbw20; "CMMG not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::He, 1, Cbw::Cbw20; "HE not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Ht, 36, Cbw::Cbw80; "invalid HT width")]
    #[test_case(false, None, fidl_common::WlanPhyType::Erp, 1, Cbw::Cbw40; "non-HT greater than 20 MHz")]
    #[test_case(false, None, fidl_common::WlanPhyType::Ht, 36, Cbw::Cbw80; "HT greater than 40 MHz")]
    #[test_case(false, Some(fake_5ghz_band_capability_ht_cbw(ChanWidthSet::TWENTY_ONLY)),
                fidl_common::WlanPhyType::Ht, 44, Cbw::Cbw40; "HT 20 MHz only")]
    #[test_case(false, Some(fidl_mlme::BandCapability {
                    ht_cap: None, ..fake_5ghz_band_capability()
                }),
                fidl_common::WlanPhyType::Ht, 48, Cbw::Cbw40; "No HT capabilities")]
    #[test_case(false, None, fidl_common::WlanPhyType::Vht, 36, Cbw::Cbw160; "160 MHz not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Vht, 36, Cbw::Cbw80P80 { secondary80: 106 }; "80+80 MHz not supported")]
    #[test_case(false, None, fidl_common::WlanPhyType::Vht, 1, Cbw::Cbw20; "VHT 2.4 GHz not supported")]
    #[test_case(false, Some(fidl_mlme::BandCapability {
                    vht_cap: None,
                    ..fake_5ghz_band_capability()
                }),
                fidl_common::WlanPhyType::Vht, 149, Cbw::Cbw40; "no VHT capabilities")]
    #[test_case(true, None, fidl_common::WlanPhyType::Hr, 1, Cbw::Cbw20)]
    #[test_case(true, None, fidl_common::WlanPhyType::Erp, 1, Cbw::Cbw20)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 1, Cbw::Cbw20)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 1, Cbw::Cbw40)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 11, Cbw::Cbw40Below)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 36, Cbw::Cbw20)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 36, Cbw::Cbw40)]
    #[test_case(true, None, fidl_common::WlanPhyType::Ht, 40, Cbw::Cbw40Below)]
    #[test_case(true, None, fidl_common::WlanPhyType::Vht, 36, Cbw::Cbw20)]
    #[test_case(true, None, fidl_common::WlanPhyType::Vht, 36, Cbw::Cbw40)]
    #[test_case(true, None, fidl_common::WlanPhyType::Vht, 40, Cbw::Cbw40Below)]
    #[test_case(true, None, fidl_common::WlanPhyType::Vht, 36, Cbw::Cbw80)]
    fn test_validate_radio_cfg(
        valid: bool,
        band_cap: Option<fidl_mlme::BandCapability>,
        phy: fidl_common::WlanPhyType,
        primary: u8,
        cbw: Cbw,
    ) {
        let channel = Channel::new(primary, cbw);
        let radio_cfg = RadioConfig { phy: phy.clone(), channel: channel.clone() };
        let expected_op_radio_cfg = OpRadioConfig { phy: phy.clone(), channel: channel.clone() };
        let band_cap = match band_cap {
            Some(band_cap) => band_cap,
            None => fake_2ghz_band_capability_vht(),
        };

        match validate_radio_cfg(&band_cap, &radio_cfg) {
            Ok(op_radio_cfg) => {
                if valid {
                    assert_eq!(op_radio_cfg, expected_op_radio_cfg);
                } else {
                    panic!("Unexpected successful validation");
                }
            }
            Err(StartResult::InvalidArguments { .. }) => {
                if valid {
                    panic!("Unexpected failure to validate.");
                }
            }
            Err(other) => {
                panic!("Unexpected StartResult value: {:?}", other);
            }
        }
    }

    #[test]
    fn authenticate_while_sme_is_idle() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = create_sme(&exec);
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));

        assert_variant!(mlme_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty");
        });
    }

    // Check status when sme is idle
    #[test]
    fn status_when_sme_is_idle() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (sme, _, _) = create_sme(&exec);
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_starts_success() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = create_sme(&exec);
        let mut receiver = sme.on_start_command(unprotected_config());

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(start_req))) => {
            assert_eq!(start_req.ssid, SSID.to_vec());
            assert_eq!(
                start_req.capability_info,
                mac::CapabilityInfo(0).with_short_preamble(true).with_ess(true).raw(),
            );
            assert_eq!(start_req.bss_type, fidl_internal::BssType::Infrastructure);
            assert_ne!(start_req.beacon_period, 0);
            assert_eq!(start_req.dtim_period, DEFAULT_DTIM_PERIOD);
            assert_eq!(
                start_req.channel,
                unprotected_config().radio_cfg.channel.primary,
            );
            assert!(start_req.rsne.is_none());
        });

        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
    }

    // Check status when Ap starting and started
    #[test]
    fn ap_starts_success_get_running_ap() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = create_sme(&exec);
        let mut receiver = sme.on_start_command(unprotected_config());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(_start_req))) => {});
        // status should be Starting
        assert_eq!(None, sme.get_running_ap());
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.channel.primary,
                num_clients: 0,
            }),
            sme.get_running_ap()
        );
    }

    // Check status after channel change
    #[test]
    fn ap_check_status_after_channel_change() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, _) = start_unprotected_ap(&exec);
        // Check status
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.channel.primary,
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
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, mut time_stream) = create_sme(&exec);
        let mut receiver = sme.on_start_command(unprotected_config());

        let (_, event) = time_stream.try_next().unwrap().expect("expect timer message");
        sme.on_timeout(event);

        assert_eq!(Ok(Some(StartResult::TimedOut)), receiver.try_recv());
        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_starts_fails() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, _) = create_sme(&exec);
        let mut receiver = sme.on_start_command(unprotected_config());

        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::NotSupported));
        assert_eq!(Ok(Some(StartResult::InternalError)), receiver.try_recv());
        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn start_req_while_ap_is_starting() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, _) = create_sme(&exec);
        let mut receiver_one = sme.on_start_command(unprotected_config());

        // While SME is starting, any start request receives an error immediately
        let mut receiver_two = sme.on_start_command(unprotected_config());
        assert_eq!(Ok(Some(StartResult::PreviousStartInProgress)), receiver_two.try_recv());

        // Start confirmation for first request should still have an affect
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::Success));
        assert_eq!(Ok(Some(StartResult::Success)), receiver_one.try_recv());
    }

    #[test]
    fn start_req_while_ap_is_stopping() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, _) = start_unprotected_ap(&exec);
        let mut stop_receiver = sme.on_stop_command();
        let mut start_receiver = sme.on_start_command(unprotected_config());
        assert_eq!(Ok(None), stop_receiver.try_recv());
        assert_eq!(Ok(Some(StartResult::Canceled)), start_receiver.try_recv());
    }

    #[test]
    fn ap_stops_while_idle() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = create_sme(&exec);
        let mut receiver = sme.on_stop_command();
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert!(stop_req.ssid.is_empty());
        });

        // Respond with a successful stop result code
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());
    }

    #[test]
    fn stop_req_while_ap_is_starting_then_succeeds() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = create_sme(&exec);
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
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::Success));
        assert_eq!(Ok(Some(StartResult::Canceled)), start_receiver.try_recv());
        assert_eq!(Ok(None), stop_receiver.try_recv());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        // Respond with a successful stop result code
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver.try_recv());
    }

    #[test]
    fn stop_req_while_ap_is_starting_then_times_out() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, mut time_stream) = create_sme(&exec);
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
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver.try_recv());
    }

    #[test]
    fn ap_stops_after_started() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let mut receiver = sme.on_stop_command();

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::BssAlreadyStopped));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());
    }

    #[test]
    fn ap_stops_after_started_and_deauths_all_clients() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);

        // Check status
        assert_eq!(
            Some(fidl_sme::Ap {
                ssid: SSID.to_vec(),
                channel: unprotected_config().radio_cfg.channel.primary,
                num_clients: 1,
            }),
            sme.get_running_ap()
        );
        let mut receiver = sme.on_stop_command();
        assert_variant!(
        mlme_stream.try_next(),
        Ok(Some(MlmeRequest::Deauthenticate(deauth_req))) => {
            assert_eq!(deauth_req.peer_sta_address, client.addr);
            assert_eq!(deauth_req.reason_code, fidl_ieee80211::ReasonCode::StaLeaving);
        });

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });
        assert_eq!(Ok(None), receiver.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver.try_recv());

        // Check status
        assert_eq!(None, sme.get_running_ap());
    }

    #[test]
    fn ap_queues_concurrent_stop_requests() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, _, _) = start_unprotected_ap(&exec);
        let mut receiver1 = sme.on_stop_command();
        let mut receiver2 = sme.on_stop_command();

        assert_eq!(Ok(None), receiver1.try_recv());
        assert_eq!(Ok(None), receiver2.try_recv());

        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver1.try_recv());
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), receiver2.try_recv());
    }

    #[test]
    fn uncleaned_stopping_state() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let mut stop_receiver1 = sme.on_stop_command();
        // Clear out the stop request
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Stop(stop_req))) => {
            assert_eq!(stop_req.ssid, SSID.to_vec());
        });

        assert_eq!(Ok(None), stop_receiver1.try_recv());
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::InternalError));
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
        sme.on_mlme_event(create_stop_conf(fidl_mlme::StopResultCode::Success));
        assert_eq!(Ok(Some(fidl_sme::StopApResultCode::Success)), stop_receiver2.try_recv());
    }

    #[test]
    fn client_authenticates_supported_authentication_type() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);
    }

    #[test]
    fn client_authenticates_unsupported_authentication_type() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let client = Client::default();
        let auth_ind = client.create_auth_ind(fidl_mlme::AuthenticationTypes::FastBssTransition);
        sme.on_mlme_event(auth_ind);
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Refused);
    }

    #[test]
    fn client_associates_unprotected_network() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let client = Client::default();
        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCode::Success,
            false,
        );
    }

    #[test]
    fn client_associates_valid_rsne() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_protected_ap(&exec);
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCode::Success,
            true,
        );
        client.verify_eapol_req(&mut mlme_stream);
    }

    #[test]
    fn client_associates_invalid_rsne() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_protected_ap(&exec);
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_refused_assoc_resp(
            &mut mlme_stream,
            fidl_mlme::AssociateResultCode::RefusedCapabilitiesMismatch,
        );
    }

    #[test]
    fn rsn_handshake_timeout() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, mut time_stream) = start_protected_ap(&exec);
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);

        // Drain the association timeout message.
        assert_variant!(time_stream.try_next(), Ok(Some(_)));

        sme.on_mlme_event(client.create_assoc_ind(Some(RSNE.to_vec())));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCode::Success,
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

        client.verify_deauth_req(
            &mut mlme_stream,
            fidl_ieee80211::ReasonCode::FourwayHandshakeTimeout,
        );
    }

    #[test]
    fn client_restarts_authentication_flow() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_unprotected_ap(&exec);
        let client = Client::default();
        client.authenticate_and_drain_mlme(&mut sme, &mut mlme_stream);
        client.associate_and_drain_mlme(&mut sme, &mut mlme_stream, None);

        sme.on_mlme_event(client.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);

        sme.on_mlme_event(client.create_assoc_ind(None));
        client.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCode::Success,
            false,
        );
    }

    #[test]
    fn multiple_clients_associate() {
        let exec = fasync::TestExecutor::new().unwrap();
        let (mut sme, mut mlme_stream, _) = start_protected_ap(&exec);
        let client1 = Client::default();
        let client2 = Client { addr: CLIENT_ADDR2 };

        sme.on_mlme_event(client1.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client1.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);

        sme.on_mlme_event(client2.create_auth_ind(fidl_mlme::AuthenticationTypes::OpenSystem));
        client2.verify_auth_resp(&mut mlme_stream, fidl_mlme::AuthenticateResultCode::Success);

        sme.on_mlme_event(client1.create_assoc_ind(Some(RSNE.to_vec())));
        client1.verify_assoc_resp(
            &mut mlme_stream,
            1,
            fidl_mlme::AssociateResultCode::Success,
            true,
        );
        client1.verify_eapol_req(&mut mlme_stream);

        sme.on_mlme_event(client2.create_assoc_ind(Some(RSNE.to_vec())));
        client2.verify_assoc_resp(
            &mut mlme_stream,
            2,
            fidl_mlme::AssociateResultCode::Success,
            true,
        );
        client2.verify_eapol_req(&mut mlme_stream);
    }

    fn create_start_conf(result_code: fidl_mlme::StartResultCode) -> MlmeEvent {
        MlmeEvent::StartConf { resp: fidl_mlme::StartConfirm { result_code } }
    }

    fn create_stop_conf(result_code: fidl_mlme::StopResultCode) -> MlmeEvent {
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
                    capability_info: mac::CapabilityInfo(0).with_short_preamble(true).raw(),
                    rates: vec![
                        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
                    ],
                },
            }
        }

        fn verify_auth_resp(
            &self,
            mlme_stream: &mut MlmeStream,
            result_code: fidl_mlme::AuthenticateResultCode,
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
            result_code: fidl_mlme::AssociateResultCode,
            privacy: bool,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::AssocResponse(assoc_resp))) => {
                assert_eq!(assoc_resp.peer_sta_address, self.addr);
                assert_eq!(assoc_resp.association_id, aid);
                assert_eq!(assoc_resp.result_code, result_code);
                assert_eq!(
                    assoc_resp.capability_info,
                    mac::CapabilityInfo(0).with_short_preamble(true).with_privacy(privacy).raw(),
                );
            });
        }

        fn verify_refused_assoc_resp(
            &self,
            mlme_stream: &mut MlmeStream,
            result_code: fidl_mlme::AssociateResultCode,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::AssocResponse(assoc_resp))) => {
                assert_eq!(assoc_resp.peer_sta_address, self.addr);
                assert_eq!(assoc_resp.association_id, 0);
                assert_eq!(assoc_resp.result_code, result_code);
                assert_eq!(assoc_resp.capability_info, 0);
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
            reason_code: fidl_ieee80211::ReasonCode,
        ) {
            let msg = mlme_stream.try_next();
            assert_variant!(msg, Ok(Some(MlmeRequest::Deauthenticate(deauth_req))) => {
                assert_eq!(deauth_req.peer_sta_address, self.addr);
                assert_eq!(deauth_req.reason_code, reason_code);
            });
        }
    }

    fn start_protected_ap(exec: &fasync::TestExecutor) -> (ApSme, crate::MlmeStream, TimeStream) {
        start_ap(true, exec)
    }

    fn start_unprotected_ap(exec: &fasync::TestExecutor) -> (ApSme, crate::MlmeStream, TimeStream) {
        start_ap(false, exec)
    }

    fn start_ap(
        protected: bool,
        exec: &fasync::TestExecutor,
    ) -> (ApSme, crate::MlmeStream, TimeStream) {
        let (mut sme, mut mlme_stream, mut time_stream) = create_sme(exec);
        let config = if protected { protected_config() } else { unprotected_config() };
        let mut receiver = sme.on_start_command(config);
        assert_eq!(Ok(None), receiver.try_recv());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Start(..))));
        // drain time stream
        while let Ok(..) = time_stream.try_next() {}
        sme.on_mlme_event(create_start_conf(fidl_mlme::StartResultCode::Success));

        assert_eq!(Ok(Some(StartResult::Success)), receiver.try_recv());
        (sme, mlme_stream, time_stream)
    }

    fn create_sme(_exec: &fasync::TestExecutor) -> (ApSme, MlmeStream, TimeStream) {
        ApSme::new(fake_device_info(AP_ADDR))
    }
}
