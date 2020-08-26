// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod link_state;

use {
    crate::{
        capabilities::{intersect_with_ap_as_client, ClientCapabilities},
        client::{
            bss::ClientConfig,
            capabilities::derive_join_channel_and_capabilities,
            event::Event,
            internal::Context,
            protection::{build_protection_ie, Protection, ProtectionIe},
            report_connect_finished, ConnectFailure, ConnectResult, EstablishRsnaFailure, Status,
        },
        clone_utils::clone_bss_desc,
        phy_selection::derive_phy_cbw,
        responder::Responder,
        sink::MlmeSink,
        timer::EventId,
        MlmeRequest,
    },
    anyhow::bail,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BssDescription, MlmeEvent},
    fuchsia_inspect_contrib::{inspect_log, log::InspectBytes},
    fuchsia_zircon as zx,
    link_state::LinkState,
    log::{error, info, warn},
    static_assertions::assert_eq_size,
    std::convert::TryInto,
    wep_deprecated,
    wlan_common::{
        bss::BssDescriptionExt,
        channel::Channel,
        format::MacFmt,
        ie::{
            self,
            rsn::{akm, cipher},
        },
        mac::Bssid,
        RadioConfig,
    },
    wlan_rsn::rsna::{AuthStatus, SecAssocUpdate, UpdateSink},
    wlan_statemachine::*,
    zerocopy::AsBytes,
};
const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

const IDLE_STATE: &str = "IdleState";
const JOINING_STATE: &str = "JoiningState";
const AUTHENTICATING_STATE: &str = "AuthenticatingState";
const ASSOCIATING_STATE: &str = "AssociatingState";
const RSNA_STATE: &str = "EstablishingRsnaState";
const LINK_UP_STATE: &str = "LinkUpState";

#[derive(Debug)]
pub struct ConnectCommand {
    pub bss: Box<BssDescription>,
    pub responder: Option<Responder<ConnectResult>>,
    pub protection: Protection,
    pub radio_cfg: RadioConfig,
}

#[derive(Debug)]
pub struct Idle {
    cfg: ClientConfig,
}

#[derive(Debug)]
pub struct Joining {
    cfg: ClientConfig,
    cmd: ConnectCommand,
    chan: Channel,
    cap: Option<ClientCapabilities>,
    protection_ie: Option<ProtectionIe>,
}

#[derive(Debug)]
pub struct Authenticating {
    cfg: ClientConfig,
    cmd: ConnectCommand,
    chan: Channel,
    cap: Option<ClientCapabilities>,
    protection_ie: Option<ProtectionIe>,
}

#[derive(Debug)]
pub struct Associating {
    cfg: ClientConfig,
    cmd: ConnectCommand,
    chan: Channel,
    cap: Option<ClientCapabilities>,
    protection_ie: Option<ProtectionIe>,
}

#[derive(Debug)]
pub struct Associated {
    cfg: ClientConfig,
    bss: Box<BssDescription>,
    last_rssi: i8,
    last_snr: i8,
    link_state: LinkState,
    radio_cfg: RadioConfig,
    chan: Channel,
    cap: Option<ClientCapabilities>,
    protection_ie: Option<ProtectionIe>,
    wmm_param: Option<ie::WmmParam>,
}

statemachine!(
    #[derive(Debug)]
    pub enum ClientState,
    () => Idle,
    Idle => Joining,
    Joining => [Authenticating, Idle],
    Authenticating => [Associating, Idle],
    Associating => [Associated, Idle],
    // We transition back to Associating on a disassociation ind.
    Associated => [Idle, Associating],
);

/// Context surrounding the state change, for Inspect logging
pub enum StateChangeContext {
    Disconnect {
        msg: String,
        reason_code: u16,
        /// True if disconnect is initiated within the device.
        /// False if disconnect happens due to frame sent by AP.
        locally_initiated: bool,
    },
    Msg(String),
}

trait StateChangeContextExt {
    fn set_msg(&mut self, msg: String);
}

impl StateChangeContextExt for Option<StateChangeContext> {
    fn set_msg(&mut self, msg: String) {
        match self {
            Some(ctx) => match ctx {
                StateChangeContext::Disconnect { msg: ref mut inner, .. } => *inner = msg,
                StateChangeContext::Msg(inner) => *inner = msg,
            },
            None => {
                self.replace(StateChangeContext::Msg(msg));
            }
        }
    }
}

impl Joining {
    fn on_join_conf(
        self,
        conf: fidl_mlme::JoinConfirm,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Authenticating, Idle> {
        match conf.result_code {
            fidl_mlme::JoinResultCodes::Success => {
                context.info.report_auth_started();
                if let Protection::Wep(ref key) = self.cmd.protection {
                    install_wep_key(context, self.cmd.bss.bssid.clone(), key);
                    context.mlme_sink.send(MlmeRequest::Authenticate(
                        wep_deprecated::make_mlme_authenticate_request(
                            self.cmd.bss.bssid.clone(),
                            DEFAULT_AUTH_FAILURE_TIMEOUT,
                        ),
                    ));
                } else {
                    let auth_type = match &self.cmd.protection {
                        Protection::Rsna(rsna) => match rsna.negotiated_protection.akm.suite_type {
                            akm::SAE => fidl_mlme::AuthenticationTypes::Sae,
                            _ => fidl_mlme::AuthenticationTypes::OpenSystem,
                        },
                        _ => fidl_mlme::AuthenticationTypes::OpenSystem,
                    };
                    context.mlme_sink.send(MlmeRequest::Authenticate(
                        fidl_mlme::AuthenticateRequest {
                            peer_sta_address: self.cmd.bss.bssid.clone(),
                            auth_type,
                            auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                        },
                    ));
                }
                state_change_ctx.set_msg("successful join".to_string());
                Ok(Authenticating {
                    cfg: self.cfg,
                    cmd: self.cmd,
                    chan: self.chan,
                    cap: self.cap,
                    protection_ie: self.protection_ie,
                })
            }
            other => {
                error!("Join request failed with result code {:?}", other);
                report_connect_finished(
                    self.cmd.responder,
                    context,
                    ConnectResult::Failed(ConnectFailure::JoinFailure(other)),
                );
                state_change_ctx.set_msg(format!("join failed; result code: {:?}", other));
                Err(Idle { cfg: self.cfg })
            }
        }
    }
}

impl Authenticating {
    fn on_authenticate_conf(
        self,
        conf: fidl_mlme::AuthenticateConfirm,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Associating, Idle> {
        match conf.result_code {
            fidl_mlme::AuthenticateResultCodes::Success => {
                context.info.report_assoc_started();
                send_mlme_assoc_req(
                    Bssid(self.cmd.bss.bssid.clone()),
                    self.cap.as_ref(),
                    &self.protection_ie,
                    &context.mlme_sink,
                );
                state_change_ctx.set_msg("successful authentication".to_string());
                Ok(Associating {
                    cfg: self.cfg,
                    cmd: self.cmd,
                    chan: self.chan,
                    cap: self.cap,

                    protection_ie: self.protection_ie,
                })
            }
            other => {
                error!("Authenticate request failed with result code {:?}", other);
                report_connect_finished(
                    self.cmd.responder,
                    context,
                    ConnectResult::Failed(ConnectFailure::AuthenticationFailure(other)),
                );
                state_change_ctx.set_msg(format!("auth failed; result code: {:?}", other));
                Err(Idle { cfg: self.cfg })
            }
        }
    }

    fn on_deauthenticate_ind(
        self,
        ind: fidl_mlme::DeauthenticateIndication,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Idle {
        error!(
            "authentication request failed due to spurious deauthentication: {:?}",
            ind.reason_code
        );
        report_connect_finished(
            self.cmd.responder,
            context,
            ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
                fidl_mlme::AuthenticateResultCodes::Refused,
            )),
        );
        state_change_ctx.replace(StateChangeContext::Disconnect {
            msg: format!(
                "received DeauthenticateInd msg while authenticating; reason code {:?}",
                ind.reason_code
            ),
            reason_code: ind.reason_code.into_primitive(),
            locally_initiated: ind.locally_initiated,
        });
        Idle { cfg: self.cfg }
    }

    // Sae management functions

    fn on_sae_handshake_ind(
        &mut self,
        ind: fidl_mlme::SaeHandshakeIndication,
        context: &mut Context,
    ) -> Result<(), anyhow::Error> {
        let supplicant = match &mut self.cmd.protection {
            Protection::Rsna(rsna) => &mut rsna.supplicant,
            _ => bail!("Unexpected SAE handshake indication"),
        };

        let mut updates = UpdateSink::default();
        supplicant.on_sae_handshake_ind(&mut updates)?;
        process_sae_updates(updates, ind.peer_sta_address, context);
        Ok(())
    }

    fn on_sae_frame_rx(
        &mut self,
        frame: fidl_mlme::SaeFrame,
        context: &mut Context,
    ) -> Result<(), anyhow::Error> {
        let peer_sta_address = frame.peer_sta_address.clone();
        let supplicant = match &mut self.cmd.protection {
            Protection::Rsna(rsna) => &mut rsna.supplicant,
            _ => bail!("Unexpected SAE frame recieved"),
        };

        let mut updates = UpdateSink::default();
        supplicant.on_sae_frame_rx(&mut updates, frame)?;
        process_sae_updates(updates, peer_sta_address, context);
        Ok(())
    }
}

impl Associating {
    fn on_associate_conf(
        self,
        conf: fidl_mlme::AssociateConfirm,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Associated, Idle> {
        let wmm_param =
            conf.wmm_param.as_ref().and_then(|p| match ie::parse_wmm_param(&p.bytes[..]) {
                Ok(param) => Some(*param),
                Err(e) => {
                    warn!(
                        "Fail parsing assoc conf WMM param. Bytes: {:?}. Error: {}",
                        &p.bytes[..],
                        e
                    );
                    None
                }
            });
        let link_state = match conf.result_code {
            fidl_mlme::AssociateResultCodes::Success => {
                context.info.report_assoc_success(context.att_id);
                if let Some(cap) = self.cap.as_ref() {
                    let negotiated_cap = intersect_with_ap_as_client(cap, &conf.into());
                    // TODO(eyw): Enable this check once we switch to Rust MLME which populates
                    // associate confirm with IEs.
                    if negotiated_cap.rates.is_empty() {
                        // This is unlikely to happen with any spec-compliant AP. In case the
                        // user somehow decided to connect to a malicious AP, reject and reset.
                        error!(
                            "Associate terminated because AP's capabilities in association \
                                 response is different from beacon"
                        );
                        report_connect_finished(
                            self.cmd.responder,
                            context,
                            ConnectResult::Failed(ConnectFailure::AssociationFailure(
                                fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch,
                            )),
                        );
                        state_change_ctx.set_msg(format!(
                            "failed associating; AP's capabilites changed between beacon and\
                                 association response"
                        ));
                        return Err(Idle { cfg: self.cfg });
                    }
                    context.mlme_sink.send(MlmeRequest::FinalizeAssociation(
                        negotiated_cap.to_fidl_negotiated_capabilities(&self.chan),
                    ))
                }
                match LinkState::new(
                    &self.cmd.bss,
                    self.cmd.responder,
                    self.cmd.protection,
                    context,
                ) {
                    Some(link_state) => link_state,
                    None => {
                        state_change_ctx.set_msg(format!("supplicant failed to start"));
                        return Err(Idle { cfg: self.cfg });
                    }
                }
            }
            other => {
                error!("Associate request failed with result code {:?}", other);
                report_connect_finished(
                    self.cmd.responder,
                    context,
                    ConnectResult::Failed(ConnectFailure::AssociationFailure(other)),
                );
                state_change_ctx.set_msg(format!("failed associating; result code: {:?}", other));
                return Err(Idle { cfg: self.cfg });
            }
        };
        state_change_ctx.set_msg("successful assoc".to_string());
        Ok(Associated {
            cfg: self.cfg,
            last_rssi: self.cmd.bss.rssi_dbm,
            last_snr: self.cmd.bss.snr_db,
            bss: self.cmd.bss,
            link_state,
            radio_cfg: self.cmd.radio_cfg,
            chan: self.chan,
            cap: self.cap,
            protection_ie: self.protection_ie,
            wmm_param,
        })
    }

    fn on_deauthenticate_ind(
        self,
        ind: fidl_mlme::DeauthenticateIndication,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Idle {
        error!(
            "association request failed due to spurious deauthentication: {:?}",
            ind.reason_code
        );
        report_connect_finished(
            self.cmd.responder,
            context,
            ConnectResult::Failed(ConnectFailure::AssociationFailure(
                fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            )),
        );
        state_change_ctx.replace(StateChangeContext::Disconnect {
            msg: format!(
                "received DeauthenticateInd msg while associating; reason code {:?}",
                ind.reason_code
            ),
            reason_code: ind.reason_code.into_primitive(),
            locally_initiated: ind.locally_initiated,
        });
        Idle { cfg: self.cfg }
    }

    fn on_disassociate_ind(
        self,
        ind: fidl_mlme::DisassociateIndication,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Idle {
        error!("association request failed due to spurious disassociation: {:?}", ind.reason_code);
        report_connect_finished(
            self.cmd.responder,
            context,
            ConnectResult::Failed(ConnectFailure::AssociationFailure(
                fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            )),
        );
        state_change_ctx.replace(StateChangeContext::Disconnect {
            msg: format!(
                "received DisassociateInd msg while associating; reason code {:?}",
                ind.reason_code
            ),
            reason_code: ind.reason_code,
            locally_initiated: ind.locally_initiated,
        });

        Idle { cfg: self.cfg }
    }
}

impl Associated {
    fn on_disassociate_ind(
        self,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Associating {
        let (responder, mut protection, connected_duration) = self.link_state.disconnect();

        if let Some(duration) = connected_duration {
            context.info.report_connection_lost(
                duration,
                self.last_rssi,
                self.bss.bssid,
                self.bss.ssid.clone(),
            );
        }

        // Client is disassociating. The ESS-SA must be kept alive but reset.
        if let Protection::Rsna(rsna) = &mut protection {
            rsna.supplicant.reset();
        }

        context.att_id += 1;
        let cmd =
            ConnectCommand { bss: self.bss, responder, protection, radio_cfg: self.radio_cfg };
        send_mlme_assoc_req(
            Bssid(cmd.bss.bssid.clone()),
            self.cap.as_ref(),
            &self.protection_ie,
            &context.mlme_sink,
        );
        state_change_ctx.set_msg("received DisassociateInd msg".to_string());
        Associating {
            cfg: self.cfg,
            cmd,
            chan: self.chan,
            cap: self.cap,
            protection_ie: self.protection_ie,
        }
    }

    fn on_deauthenticate_ind(
        self,
        ind: fidl_mlme::DeauthenticateIndication,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Idle {
        let (responder, _, connected_duration) = self.link_state.disconnect();
        match connected_duration {
            Some(duration) => {
                context.info.report_connection_lost(
                    duration,
                    self.last_rssi,
                    self.bss.bssid,
                    self.bss.ssid,
                );
            }
            None => {
                let connect_result = EstablishRsnaFailure::InternalError.into();
                report_connect_finished(responder, context, connect_result);
            }
        }

        state_change_ctx.replace(StateChangeContext::Disconnect {
            msg: format!("received DeauthenticateInd msg; reason code {:?}", ind.reason_code),
            reason_code: ind.reason_code.into_primitive(),
            locally_initiated: ind.locally_initiated,
        });
        Idle { cfg: self.cfg }
    }

    fn on_eapol_ind(
        self,
        ind: fidl_mlme::EapolIndication,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, Idle> {
        // Ignore unexpected EAPoL frames.
        if !self.bss.needs_eapol_exchange() {
            return Ok(self);
        }

        // Reject EAPoL frames from other BSS.
        if ind.src_addr != self.bss.bssid {
            let eapol_pdu = &ind.data[..];
            inspect_log!(context.inspect.rsn_events.lock(), {
                rx_eapol_frame: InspectBytes(&eapol_pdu),
                foreign_bssid: ind.src_addr.to_mac_str(),
                foreign_bssid_hash: context.inspect.hasher.hash_mac_addr(ind.src_addr),
                current_bssid: self.bss.bssid.to_mac_str(),
                current_bssid_hash: context.inspect.hasher.hash_mac_addr(self.bss.bssid),
                status: "rejected (foreign BSS)",
            });
            return Ok(self);
        }

        let link_state =
            match self.link_state.on_eapol_ind(ind, &self.bss, state_change_ctx, context) {
                Some(link_state) => link_state,
                None => return Err(Idle { cfg: self.cfg }),
            };
        Ok(Self { link_state, ..self })
    }

    fn on_channel_switched(&mut self, info: fidl_mlme::ChannelSwitchInfo) {
        // Right now we just update the stored bss description, but we may want to provide some
        // notice or metric for this in the future.
        self.bss.chan.primary = info.new_channel
    }

    fn handle_timeout(
        self,
        event_id: EventId,
        event: Event,
        state_change_ctx: &mut Option<StateChangeContext>,
        context: &mut Context,
    ) -> Result<Self, Idle> {
        match self.link_state.handle_timeout(event_id, event, state_change_ctx, context) {
            Some(link_state) => Ok(Associated { link_state, ..self }),
            None => {
                send_deauthenticate_request(&self.bss, &context.mlme_sink);
                Err(Idle { cfg: self.cfg })
            }
        }
    }
}

impl ClientState {
    pub fn new(cfg: ClientConfig) -> Self {
        Self::from(State::new(Idle { cfg }))
    }

    fn state_name(&self) -> &'static str {
        match self {
            Self::Idle(_) => IDLE_STATE,
            Self::Joining(_) => JOINING_STATE,
            Self::Authenticating(_) => AUTHENTICATING_STATE,
            Self::Associating(_) => ASSOCIATING_STATE,
            Self::Associated(state) => match state.link_state {
                LinkState::EstablishingRsna(_) => RSNA_STATE,
                LinkState::LinkUp(_) => LINK_UP_STATE,
                _ => unreachable!(),
            },
        }
    }

    pub fn on_mlme_event(self, event: MlmeEvent, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_ctx: Option<StateChangeContext> = None;

        let new_state = match self {
            Self::Idle(_) => {
                warn!("Unexpected MLME message while Idle: {:?}", event);
                self
            }
            Self::Joining(state) => match event {
                MlmeEvent::JoinConf { resp } => {
                    let (transition, joining) = state.release_data();
                    match joining.on_join_conf(resp, &mut state_change_ctx, context) {
                        Ok(authenticating) => transition.to(authenticating).into(),
                        Err(idle) => transition.to(idle).into(),
                    }
                }
                _ => state.into(),
            },
            Self::Authenticating(state) => match event {
                MlmeEvent::AuthenticateConf { resp } => {
                    let (transition, authenticating) = state.release_data();
                    match authenticating.on_authenticate_conf(resp, &mut state_change_ctx, context)
                    {
                        Ok(associating) => transition.to(associating).into(),
                        Err(idle) => transition.to(idle).into(),
                    }
                }
                MlmeEvent::OnSaeHandshakeInd { ind } => {
                    let (transition, mut authenticating) = state.release_data();
                    if let Err(e) = authenticating.on_sae_handshake_ind(ind, context) {
                        error!("Failed to process SaeHandshakeInd: {:?}", e);
                    }
                    transition.to(authenticating).into()
                }
                MlmeEvent::OnSaeFrameRx { frame } => {
                    let (transition, mut authenticating) = state.release_data();
                    if let Err(e) = authenticating.on_sae_frame_rx(frame, context) {
                        error!("Failed to process SaeFrameRx: {:?}", e);
                    }
                    transition.to(authenticating).into()
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    let (transition, authenticating) = state.release_data();
                    let idle =
                        authenticating.on_deauthenticate_ind(ind, &mut state_change_ctx, context);
                    transition.to(idle).into()
                }
                _ => state.into(),
            },
            Self::Associating(state) => match event {
                MlmeEvent::AssociateConf { resp } => {
                    let (transition, associating) = state.release_data();
                    match associating.on_associate_conf(resp, &mut state_change_ctx, context) {
                        Ok(associated) => transition.to(associated).into(),
                        Err(idle) => transition.to(idle).into(),
                    }
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    let (transition, associating) = state.release_data();
                    let idle =
                        associating.on_deauthenticate_ind(ind, &mut state_change_ctx, context);
                    transition.to(idle).into()
                }
                MlmeEvent::DisassociateInd { ind } => {
                    let (transition, associating) = state.release_data();
                    let idle = associating.on_disassociate_ind(ind, &mut state_change_ctx, context);
                    transition.to(idle).into()
                }
                _ => state.into(),
            },
            Self::Associated(mut state) => match event {
                MlmeEvent::DisassociateInd { .. } => {
                    let (transition, associated) = state.release_data();
                    let associating =
                        associated.on_disassociate_ind(&mut state_change_ctx, context);
                    transition.to(associating).into()
                }
                MlmeEvent::DeauthenticateInd { ind } => {
                    let (transition, associated) = state.release_data();
                    let idle =
                        associated.on_deauthenticate_ind(ind, &mut state_change_ctx, context);
                    transition.to(idle).into()
                }
                MlmeEvent::SignalReport { ind } => {
                    state.last_rssi = ind.rssi_dbm;
                    state.last_snr = ind.snr_db;
                    state.into()
                }
                MlmeEvent::EapolInd { ind } => {
                    let (transition, associated) = state.release_data();
                    match associated.on_eapol_ind(ind, &mut state_change_ctx, context) {
                        Ok(associated) => transition.to(associated).into(),
                        Err(idle) => transition.to(idle).into(),
                    }
                }
                MlmeEvent::OnChannelSwitched { info } => {
                    state.on_channel_switched(info);
                    state.into()
                }
                _ => state.into(),
            },
        };

        log_state_change(start_state, &new_state, state_change_ctx, context);
        new_state
    }

    pub fn handle_timeout(self, event_id: EventId, event: Event, context: &mut Context) -> Self {
        let start_state = self.state_name();
        let mut state_change_ctx: Option<StateChangeContext> = None;

        let new_state = match self {
            Self::Associated(state) => {
                let (transition, associated) = state.release_data();
                match associated.handle_timeout(event_id, event, &mut state_change_ctx, context) {
                    Ok(associated) => transition.to(associated).into(),
                    Err(idle) => transition.to(idle).into(),
                }
            }
            _ => self,
        };

        log_state_change(start_state, &new_state, state_change_ctx, context);
        new_state
    }

    pub fn connect(self, cmd: ConnectCommand, context: &mut Context) -> Self {
        let (chan, cap) = match derive_join_channel_and_capabilities(
            Channel::from_fidl(cmd.bss.chan),
            cmd.radio_cfg.cbw,
            &cmd.bss.rates[..],
            &context.device_info,
        ) {
            Ok(chan_and_cap) => chan_and_cap,
            Err(e) => {
                error!("Failed building join capabilities: {}", e);
                return self;
            }
        };

        let cap = if context.is_softmac { Some(cap) } else { None };

        // Derive RSN (for WPA2) or Vendor IEs (for WPA1) or neither(WEP/non-protected).
        let protection_ie = match build_protection_ie(&cmd.protection) {
            Ok(ie) => ie,
            Err(e) => {
                error!("Failed to build protection IEs: {}", e);
                return self;
            }
        };

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
            bssid: cmd.bss.bssid.to_mac_str(),
            bssid_hash: context.inspect.hasher.hash_mac_addr(cmd.bss.bssid),
            ssid: String::from_utf8_lossy(&cmd.bss.ssid[..]).as_ref(),
            ssid_hash: context.inspect.hasher.hash(&cmd.bss.ssid[..]),
        });
        let state = Self::new(cfg.clone());
        match state {
            Self::Idle(state) => {
                state.transition_to(Joining { cfg, cmd, chan, cap, protection_ie }).into()
            }
            _ => unreachable!(),
        }
    }

    pub fn disconnect(self, context: &mut Context) -> Self {
        if let Self::Associated(state) = &self {
            context.info.report_manual_disconnect(state.bss.ssid.clone());
        }
        let start_state = self.state_name();
        let new_state = Self::new(self.disconnect_internal(context));

        let state_change_ctx = Some(StateChangeContext::Disconnect {
            msg: "disconnect command".to_string(),
            reason_code: fidl_mlme::ReasonCode::UnspecifiedReason.into_primitive(),
            locally_initiated: true,
        });
        log_state_change(start_state, &new_state, state_change_ctx, context);
        new_state
    }

    fn disconnect_internal(self, context: &mut Context) -> ClientConfig {
        match self {
            Self::Idle(state) => state.cfg,
            Self::Joining(state) => {
                let (_, state) = state.release_data();
                report_connect_finished(state.cmd.responder, context, ConnectResult::Canceled);
                state.cfg
            }
            Self::Authenticating(state) => {
                let (_, state) = state.release_data();
                report_connect_finished(state.cmd.responder, context, ConnectResult::Canceled);
                state.cfg
            }
            Self::Associating(state) => {
                let (_, state) = state.release_data();
                report_connect_finished(state.cmd.responder, context, ConnectResult::Canceled);
                send_deauthenticate_request(&state.cmd.bss, &context.mlme_sink);
                state.cfg
            }
            Self::Associated(state) => {
                send_deauthenticate_request(&state.bss, &context.mlme_sink);
                state.cfg
            }
        }
    }

    // Cancel any connect that is in progress. No-op if client is already idle or connected.
    pub fn cancel_ongoing_connect(self, context: &mut Context) -> Self {
        // Only move to idle if client is not already connected. Technically, SME being in
        // transition state does not necessarily mean that a (manual) connect attempt is
        // in progress (since DisassociateInd moves SME to transition state). However, the
        // main thing we are concerned about is that we don't disconnect from an already
        // connected state until the new connect attempt succeeds in selecting BSS.
        if self.in_transition_state() {
            Self::new(self.disconnect_internal(context))
        } else {
            self
        }
    }

    fn in_transition_state(&self) -> bool {
        match self {
            Self::Idle(_) => false,
            Self::Associated(state) => match state.link_state {
                LinkState::LinkUp { .. } => false,
                _ => true,
            },
            _ => true,
        }
    }

    pub fn status(&self) -> Status {
        match self {
            Self::Idle(_) => Status { connected_to: None, connecting_to: None },
            Self::Joining(joining) => {
                Status { connected_to: None, connecting_to: Some(joining.cmd.bss.ssid.clone()) }
            }
            Self::Authenticating(authenticating) => Status {
                connected_to: None,
                connecting_to: Some(authenticating.cmd.bss.ssid.clone()),
            },
            Self::Associating(associating) => {
                Status { connected_to: None, connecting_to: Some(associating.cmd.bss.ssid.clone()) }
            }
            Self::Associated(associated) => match associated.link_state {
                LinkState::EstablishingRsna { .. } => {
                    Status { connected_to: None, connecting_to: Some(associated.bss.ssid.clone()) }
                }
                LinkState::LinkUp { .. } => Status {
                    connected_to: {
                        let mut bss = associated
                            .cfg
                            .convert_bss_description(&associated.bss, associated.wmm_param);
                        bss.rx_dbm = associated.last_rssi;
                        bss.snr_db = associated.last_snr;
                        Some(bss)
                    },
                    connecting_to: None,
                },
                _ => unreachable!(),
            },
        }
    }
}

fn process_sae_updates(updates: UpdateSink, peer_sta_address: [u8; 6], context: &mut Context) {
    for update in updates {
        match update {
            SecAssocUpdate::TxSaeFrame(frame) => {
                context.mlme_sink.send(MlmeRequest::SaeFrameTx(frame));
            }
            SecAssocUpdate::SaeAuthStatus(status) => context.mlme_sink.send(
                MlmeRequest::SaeHandshakeResp(fidl_mlme::SaeHandshakeResponse {
                    peer_sta_address,
                    result_code: match status {
                        AuthStatus::Success => fidl_mlme::AuthenticateResultCodes::Success,
                        AuthStatus::Rejected => fidl_mlme::AuthenticateResultCodes::Refused,
                        AuthStatus::InternalError => fidl_mlme::AuthenticateResultCodes::Refused,
                    },
                }),
            ),
            _ => (),
        }
    }
}

fn log_state_change(
    start_state: &str,
    new_state: &ClientState,
    state_change_ctx: Option<StateChangeContext>,
    context: &mut Context,
) {
    if start_state == new_state.state_name() && state_change_ctx.is_none() {
        return;
    }

    match state_change_ctx {
        Some(inner) => match inner {
            // Only log the `disconnect_ctx` if an operation had an effect of moving from
            // non-idle state to idle state. This is so that the client that consumes
            // `disconnect_ctx` does not log a disconnect event when it's effectively no-op.
            StateChangeContext::Disconnect { msg, reason_code, locally_initiated }
                if start_state != IDLE_STATE =>
            {
                info!(
                    "{} => {}, ctx: `{}`, locally_initiated: {}",
                    start_state,
                    new_state.state_name(),
                    msg,
                    locally_initiated
                );

                inspect_log!(context.inspect.state_events.lock(), {
                    from: start_state,
                    to: new_state.state_name(),
                    ctx: msg,
                    disconnect_ctx: {
                        reason_code: reason_code,
                        locally_initiated: locally_initiated,
                    }
                });
            }
            StateChangeContext::Disconnect { msg, .. } | StateChangeContext::Msg(msg) => {
                inspect_log!(context.inspect.state_events.lock(), {
                    from: start_state,
                    to: new_state.state_name(),
                    ctx: msg,
                });
            }
        },
        None => {
            inspect_log!(context.inspect.state_events.lock(), {
                from: start_state,
                to: new_state.state_name(),
            });
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

/// Custom logging for ConnectCommand because its normal full debug string is too large, and we
/// want to reduce how much we log in memory for Inspect. Additionally, in the future, we'd need
/// to anonymize information like BSSID and SSID.
fn connect_cmd_inspect_summary(cmd: &ConnectCommand) -> String {
    let bss = &cmd.bss;
    format!(
        "ConnectCmd {{ \
         cap: {cap:?}, rates: {rates:?}, \
         protected: {protected:?}, chan: {chan:?}, \
         rcpi: {rcpi:?}, rsni: {rsni:?}, rssi: {rssi:?}, ht_cap: {ht_cap:?}, ht_op: {ht_op:?}, \
         vht_cap: {vht_cap:?}, vht_op: {vht_op:?} }}",
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

fn send_deauthenticate_request(current_bss: &BssDescription, mlme_sink: &MlmeSink) {
    mlme_sink.send(MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
        peer_sta_address: current_bss.bssid.clone(),
        reason_code: fidl_mlme::ReasonCode::StaLeaving,
    }));
}

fn send_mlme_assoc_req(
    bssid: Bssid,
    capabilities: Option<&ClientCapabilities>,
    protection_ie: &Option<ProtectionIe>,
    mlme_sink: &MlmeSink,
) {
    assert_eq_size!(ie::HtCapabilities, [u8; fidl_mlme::HT_CAP_LEN as usize]);
    let ht_cap = capabilities.map_or(None, |c| {
        c.0.ht_cap.map(|h| fidl_mlme::HtCapabilities { bytes: h.as_bytes().try_into().unwrap() })
    });

    assert_eq_size!(ie::VhtCapabilities, [u8; fidl_mlme::VHT_CAP_LEN as usize]);
    let vht_cap = capabilities.map_or(None, |c| {
        c.0.vht_cap.map(|v| fidl_mlme::VhtCapabilities { bytes: v.as_bytes().try_into().unwrap() })
    });
    let (rsne, vendor_ies) = match protection_ie.as_ref() {
        Some(ProtectionIe::Rsne(vec)) => (Some(vec.to_vec()), None),
        Some(ProtectionIe::VendorIes(vec)) => (None, Some(vec.to_vec())),
        None => (None, None),
    };
    let req = fidl_mlme::AssociateRequest {
        peer_sta_address: bssid.0,
        cap_info: capabilities.map_or(0, |c| c.0.cap_info.raw()),
        rates: capabilities.map_or_else(|| vec![], |c| c.0.rates.as_bytes().to_vec()),
        // TODO(43938): populate `qos_capable` field from device info
        qos_capable: ht_cap.is_some(),
        qos_info: 0,
        ht_cap: ht_cap.map(Box::new),
        vht_cap: vht_cap.map(Box::new),
        rsne,
        vendor_ies,
    };
    mlme_sink.send(MlmeRequest::Associate(req))
}

fn now() -> zx::Time {
    zx::Time::get(zx::ClockId::Monotonic)
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;
    use fuchsia_inspect::Inspector;
    use futures::channel::{mpsc, oneshot};
    use link_state::{EstablishingRsna, LinkUp};
    use std::sync::Arc;
    use wlan_common::{assert_variant, ie::rsn::rsne::RsnCapabilities, RadioConfig};
    use wlan_rsn::{key::exchange::Key, rsna::SecAssocStatus};
    use wlan_rsn::{
        rsna::{SecAssocUpdate, UpdateSink},
        NegotiatedProtection,
    };

    use crate::client::test_utils::{
        create_assoc_conf, create_auth_conf, create_join_conf, expect_info_event,
        expect_stream_empty, fake_negotiated_channel_and_capabilities,
        fake_protected_bss_description, fake_unprotected_bss_description, fake_wep_bss_description,
        fake_wmm_param, fake_wpa1_bss_description, mock_supplicant, MockSupplicant,
        MockSupplicantController,
    };
    use crate::client::{info::InfoReporter, inspect, rsn::Rsna, InfoEvent, InfoSink, TimeStream};
    use crate::test_utils::make_wpa1_ie;

    use crate::{test_utils, timer, InfoStream, MlmeStream, Ssid};

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
        expect_finalize_association_req(
            &mut h.mlme_stream,
            fake_negotiated_channel_and_capabilities(),
        );

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
        expect_finalize_association_req(
            &mut h.mlme_stream,
            fake_negotiated_channel_and_capabilities(),
        );

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
        let state = ClientState::from(testing::new_state(Joining {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        }));

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
        let state = ClientState::from(testing::new_state(Authenticating {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        }));

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
        let state = ClientState::from(testing::new_state(Associating {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        }));

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
                locally_initiated: false,
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
                locally_initiated: false,
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
                locally_initiated: false,
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
        assert_variant!(s, ClientState::Associated(state) => {
            assert_variant!(&state.link_state, LinkState::EstablishingRsna { .. })});

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
        assert_variant!(s, ClientState::Associated(state) => {
            assert_variant!(&state.link_state, LinkState::EstablishingRsna { .. })});

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
        assert_variant!(state, ClientState::Associated(state) => {
            assert_variant!(
                &state.link_state,
                LinkState::LinkUp(state) => assert_variant!(&state.protection, Protection::Rsna(_))
            )
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
        let state = ClientState::from(testing::new_state(Associating {
            cfg: ClientConfig::default(),
            cmd: command,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        }));
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
        assert_variant!(&state, ClientState::Associated(state) => {
            assert_variant!(&state.link_state, LinkState::LinkUp { .. });
        });

        // Any timeout is ignored
        let (_, timed_event) = h.time_stream.try_next().unwrap().expect("expect timed event");
        state = state.handle_timeout(timed_event.id, timed_event.event, &mut h.context);
        assert_variant!(&state, ClientState::Associated(state) => {
            assert_variant!(&state.link_state, LinkState::LinkUp { .. });
        });
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
                locally_initiated: false,
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
        let state = ClientState::from(testing::new_state(Associating {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        }));
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
                locally_initiated: true,
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
                locally_initiated: true,
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

        assert_variant!(&state, ClientState::Associated(state) => {
            assert_eq!(state.bss.chan.primary, 1);
        });
        let state = state.on_mlme_event(switch_ind, &mut h.context);
        assert_variant!(state, ClientState::Associated(state) => {
            assert_eq!(state.bss.chan.primary, 36);
        });
    }

    #[test]
    fn join_failure_capabilities_incompatible_softmac() {
        let (mut command, _receiver) = connect_command_one();
        // empty rates will cause build_join_capabilities to fail, which in turn fails the join.
        command.bss.rates = vec![];

        let mut h = TestHelper::new();
        let state = idle_state().connect(command, &mut h.context);

        // State did not change to Joining because the command was ignored due to incompatibility.
        assert_variant!(state, ClientState::Idle(_));
    }

    #[test]
    fn join_failure_capabilities_incompatible_fullmac() {
        let (mut command, _receiver) = connect_command_one();
        // empty rates will cause build_join_capabilities to fail, which in turn fails the join.
        command.bss.rates = vec![];

        let mut h = TestHelper::new();
        // set as full mac
        h.context.is_softmac = false;

        let state = idle_state().connect(command, &mut h.context);

        // State did not change to Joining because the command was ignored due to incompatibility.
        assert_variant!(state, ClientState::Idle(_));
    }

    #[test]
    fn join_success_softmac() {
        let (command, _receiver) = connect_command_one();
        let mut h = TestHelper::new();
        let state = idle_state().connect(command, &mut h.context);

        // State changed to Joining, capabilities preserved.
        let cap = assert_variant!(&state, ClientState::Joining(state) => &state.cap);
        assert!(cap.is_some());
    }

    #[test]
    fn join_success_fullmac() {
        let (command, _receiver) = connect_command_one();
        let mut h = TestHelper::new();
        // set full mac
        h.context.is_softmac = false;
        let state = idle_state().connect(command, &mut h.context);

        // State changed to Joining, capabilities discarded as FullMAC ignore them anyway.
        let cap = assert_variant!(&state, ClientState::Joining(state) => &state.cap);
        assert!(cap.is_none());
    }

    #[test]
    fn join_failure_rsne_wrapped_in_legacy_wpa() {
        let (supplicant, _suppl_mock) = mock_supplicant();

        let (mut command, _receiver) = connect_command_rsna(supplicant);
        // Take the RSNA and wrap it in LegacyWpa to make it invalid.
        if let Protection::Rsna(rsna) = command.protection {
            command.protection = Protection::LegacyWpa(rsna);
        } else {
            panic!("command is guaranteed to be contain legacy wpa");
        };

        let mut h = TestHelper::new();
        let state = idle_state().connect(command, &mut h.context);

        // State did not change to Joining because command is invalid, thus ignored.
        assert_variant!(state, ClientState::Idle(_));
    }

    #[test]
    fn join_failure_legacy_wpa_wrapped_in_rsna() {
        let (supplicant, _suppl_mock) = mock_supplicant();

        let (mut command, _receiver) = connect_command_wpa1(supplicant);
        // Take the LegacyWpa RSNA and wrap it in Rsna to make it invalid.
        if let Protection::LegacyWpa(rsna) = command.protection {
            command.protection = Protection::Rsna(rsna);
        } else {
            panic!("command is guaranteed to be contain legacy wpa");
        };

        let mut h = TestHelper::new();
        let state = idle_state();
        let state = state.connect(command, &mut h.context);

        // State did not change to Joining because command is invalid, thus ignored.
        assert_variant!(state, ClientState::Idle(_));
    }

    #[test]
    fn fill_wmm_ie_associating() {
        let mut h = TestHelper::new();
        let (cmd, _receiver) = connect_command_one();
        let resp = fidl_mlme::AssociateConfirm {
            result_code: fidl_mlme::AssociateResultCodes::Success,
            association_id: 1,
            cap_info: 0,
            rates: vec![0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
            ht_cap: cmd.bss.ht_cap.clone(),
            vht_cap: cmd.bss.vht_cap.clone(),
            wmm_param: Some(Box::new(fake_wmm_param())),
        };

        let state = associating_state(cmd);
        let state = state.on_mlme_event(MlmeEvent::AssociateConf { resp }, &mut h.context);
        assert_variant!(state, ClientState::Associated(state) => {
            assert!(state.wmm_param.is_some());
        });
    }

    #[test]
    fn status_returns_last_rssi_snr() {
        let mut h = TestHelper::new();

        let state = link_up_state(Box::new(unprotected_bss(b"RSSI".to_vec(), [42; 6])));
        let state = state.on_mlme_event(signal_report_with_rssi_snr(-42, 20), &mut h.context);
        assert_eq!(state.status().connected_to.unwrap().rx_dbm, -42);
        assert_eq!(state.status().connected_to.unwrap().snr_db, 20);

        let state = state.on_mlme_event(signal_report_with_rssi_snr(-24, 10), &mut h.context);
        assert_eq!(state.status().connected_to.unwrap().rx_dbm, -24);
        assert_eq!(state.status().connected_to.unwrap().snr_db, 10);
    }

    // Helper functions and data structures for tests
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
            let inspect_hash_key = [88, 77, 66, 55, 44, 33, 22, 11];
            let context = Context {
                device_info: Arc::new(fake_device_info()),
                mlme_sink: MlmeSink::new(mlme_sink),
                timer,
                att_id: 0,
                inspect: Arc::new(inspect::SmeTree::new(inspector.root(), inspect_hash_key)),
                info: InfoReporter::new(InfoSink::new(info_sink)),
                is_softmac: true,
            };
            TestHelper { mlme_stream, info_stream, time_stream, context, _inspector: inspector }
        }
    }

    fn on_eapol_ind(
        state: ClientState,
        helper: &mut TestHelper,
        bssid: [u8; 6],
        suppl_mock: &MockSupplicantController,
        update_sink: UpdateSink,
    ) -> ClientState {
        suppl_mock.set_on_eapol_frame_results(update_sink);
        // (mlme->sme) Send an EapolInd
        let eapol_ind = create_eapol_ind(bssid.clone(), test_utils::eapol_key_frame().into());
        state.on_mlme_event(eapol_ind, &mut helper.context)
    }

    fn create_eapol_ind(bssid: [u8; 6], data: Vec<u8>) -> MlmeEvent {
        MlmeEvent::EapolInd {
            ind: fidl_mlme::EapolIndication {
                src_addr: bssid,
                dst_addr: fake_device_info().mac_addr,
                data,
            },
        }
    }

    fn exchange_deauth(state: ClientState, h: &mut TestHelper) -> ClientState {
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

    fn expect_finalize_association_req(
        mlme_stream: &mut MlmeStream,
        chan_and_cap: (Channel, ClientCapabilities),
    ) {
        let (chan, join_cap) = chan_and_cap;
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::FinalizeAssociation(cap))) => {
            assert_eq!(cap, join_cap.0.to_fidl_negotiated_capabilities(&chan));
        });
    }

    fn expect_eapol_req(mlme_stream: &mut MlmeStream, bssid: [u8; 6]) {
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Eapol(req))) => {
            assert_eq!(req.src_addr, fake_device_info().mac_addr);
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

    fn idle_state() -> ClientState {
        testing::new_state(Idle { cfg: ClientConfig::default() }).into()
    }

    fn assert_idle(state: ClientState) {
        assert_variant!(&state, ClientState::Idle(_));
    }

    fn joining_state(cmd: ConnectCommand) -> ClientState {
        testing::new_state(Joining {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        })
        .into()
    }

    fn assert_joining(state: ClientState, bss: &BssDescription) {
        assert_variant!(&state, ClientState::Joining(joining) => {
            assert_eq!(joining.cmd.bss.as_ref(), bss);
        });
    }

    fn authenticating_state(cmd: ConnectCommand) -> ClientState {
        testing::new_state(Authenticating {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        })
        .into()
    }

    fn associating_state(cmd: ConnectCommand) -> ClientState {
        testing::new_state(Associating {
            cfg: ClientConfig::default(),
            cmd,
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
        })
        .into()
    }

    fn assert_associating(state: ClientState, bss: &BssDescription) {
        assert_variant!(&state, ClientState::Associating(associating) => {
            assert_eq!(associating.cmd.bss.as_ref(), bss);
        });
    }

    fn establishing_rsna_state(cmd: ConnectCommand) -> ClientState {
        let rsna = assert_variant!(cmd.protection, Protection::Rsna(rsna) => rsna);
        let link_state = testing::new_state(EstablishingRsna {
            responder: cmd.responder,
            rsna,
            rsna_timeout: None,
            resp_timeout: None,
        })
        .into();
        testing::new_state(Associated {
            cfg: ClientConfig::default(),
            bss: cmd.bss,
            last_rssi: 60,
            last_snr: 0,
            link_state,
            radio_cfg: RadioConfig::default(),
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
            wmm_param: None,
        })
        .into()
    }

    fn link_up_state(bss: Box<fidl_mlme::BssDescription>) -> ClientState {
        let link_state = testing::new_state(LinkUp {
            protection: Protection::Open,
            since: now(),
            ping_event: None,
        })
        .into();
        testing::new_state(Associated {
            cfg: ClientConfig::default(),
            bss,
            last_rssi: 60,
            last_snr: 0,
            link_state,
            radio_cfg: RadioConfig::default(),
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
            wmm_param: None,
        })
        .into()
    }

    fn link_up_state_protected(supplicant: MockSupplicant, bssid: [u8; 6]) -> ClientState {
        let bss = protected_bss(b"foo".to_vec(), bssid);
        let rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
        let rsna = Rsna {
            negotiated_protection: NegotiatedProtection::from_rsne(&rsne)
                .expect("invalid NegotiatedProtection"),
            supplicant: Box::new(supplicant),
        };
        let link_state = testing::new_state(LinkUp {
            protection: Protection::Rsna(rsna),
            since: now(),
            ping_event: None,
        })
        .into();
        testing::new_state(Associated {
            cfg: ClientConfig::default(),
            bss: Box::new(bss),
            last_rssi: 60,
            last_snr: 0,
            link_state,
            radio_cfg: RadioConfig::default(),
            chan: fake_channel(),
            cap: None,
            protection_ie: None,
            wmm_param: None,
        })
        .into()
    }

    fn protected_bss(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
        fidl_mlme::BssDescription { bssid, ..fake_protected_bss_description(ssid) }
    }

    fn unprotected_bss(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
        fidl_mlme::BssDescription { bssid, ..fake_unprotected_bss_description(ssid) }
    }

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        test_utils::fake_device_info([0, 1, 2, 3, 4, 5])
    }

    fn fake_channel() -> Channel {
        Channel { primary: 153, cbw: wlan_common::channel::Cbw::Cbw20 }
    }

    fn signal_report_with_rssi_snr(rssi_dbm: i8, snr_db: i8) -> MlmeEvent {
        MlmeEvent::SignalReport { ind: fidl_mlme::SignalReportIndication { rssi_dbm, snr_db } }
    }
}
