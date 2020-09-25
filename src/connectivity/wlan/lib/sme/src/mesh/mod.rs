// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        clone_utils, phy_selection::get_device_band_info, responder::Responder, sink::MlmeSink,
        timer::TimedEvent, MlmeRequest,
    },
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo, MlmeEvent},
    futures::channel::{mpsc, oneshot},
    log::error,
    std::mem,
    wlan_common::{
        channel::{Cbw, Channel},
        mac::Aid,
    },
};

const DEFAULT_BEACON_PERIOD: u16 = 1000;
const DEFAULT_DTIM_PERIOD: u8 = 1;

// A list of pending join/leave requests to be maintained in the intermediate
// 'Joining'/'Leaving' states where we are waiting for a reply from MLME and cannot
// serve the requests immediately.
struct PendingRequests {
    leave: Vec<Responder<LeaveMeshResult>>,
    join: Option<(Responder<JoinMeshResult>, Config)>,
}

impl PendingRequests {
    pub fn new() -> Self {
        PendingRequests { leave: Vec::new(), join: None }
    }

    pub fn enqueue_leave(&mut self, responder: Responder<LeaveMeshResult>) {
        self.replace_join_request(None);
        self.leave.push(responder);
    }

    pub fn enqueue_join(&mut self, responder: Responder<JoinMeshResult>, config: Config) {
        self.replace_join_request(Some((responder, config)));
    }

    pub fn is_empty(&self) -> bool {
        self.leave.is_empty() && self.join.is_none()
    }

    fn replace_join_request(&mut self, req: Option<(Responder<JoinMeshResult>, Config)>) {
        if let Some((old_responder, _)) = mem::replace(&mut self.join, req) {
            old_responder.respond(JoinMeshResult::Canceled);
        }
    }
}

enum State {
    Idle,
    Joining { responder: Responder<JoinMeshResult>, config: Config, pending: PendingRequests },
    Joined { config: Config },
    Leaving { config: Config, pending: PendingRequests },
}

pub struct MeshSme {
    mlme_sink: MlmeSink,
    state: Option<State>,
    device_info: DeviceInfo,
}

pub type MeshId = Vec<u8>;

#[derive(Debug)]
pub struct Config {
    pub mesh_id: MeshId,
    pub channel: u8,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum JoinMeshResult {
    Success,
    Canceled,
    InternalError,
    InvalidArguments,
    DfsUnsupported,
}

#[derive(Clone, Copy, Debug)]
pub enum LeaveMeshResult {
    Success,
    InternalError,
}

impl MeshSme {
    pub fn on_join_command(&mut self, config: Config) -> oneshot::Receiver<JoinMeshResult> {
        let (responder, receiver) = Responder::new();
        if let Err(result) = validate_config(&config) {
            responder.respond(result);
            return receiver;
        }
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => {
                self.mlme_sink.send(MlmeRequest::Start(create_start_request(&config)));
                State::Joining { responder, pending: PendingRequests::new(), config }
            }
            State::Joining { responder: cur_responder, config: cur_config, mut pending } => {
                pending.enqueue_join(responder, config);
                State::Joining { responder: cur_responder, config: cur_config, pending }
            }
            State::Joined { config: cur_config } => {
                self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                let mut pending = PendingRequests::new();
                pending.enqueue_join(responder, config);
                State::Leaving { config: cur_config, pending }
            }
            State::Leaving { config: cur_config, mut pending } => {
                pending.enqueue_join(responder, config);
                State::Leaving { config: cur_config, pending }
            }
        });
        receiver
    }

    pub fn on_leave_command(&mut self) -> oneshot::Receiver<LeaveMeshResult> {
        let (responder, receiver) = Responder::new();
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => {
                responder.respond(LeaveMeshResult::Success);
                State::Idle
            }
            State::Joining { responder: cur_responder, config, mut pending } => {
                pending.enqueue_leave(responder);
                State::Joining { responder: cur_responder, pending, config }
            }
            State::Joined { config } => {
                self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                let mut pending = PendingRequests::new();
                pending.enqueue_leave(responder);
                State::Leaving { config, pending }
            }
            State::Leaving { config, mut pending } => {
                pending.enqueue_leave(responder);
                State::Leaving { config, pending }
            }
        });
        receiver
    }
}

fn on_back_to_idle(pending: PendingRequests, mlme_sink: &MlmeSink) -> State {
    for responder in pending.leave {
        responder.respond(LeaveMeshResult::Success);
    }
    if let Some((responder, config)) = pending.join {
        mlme_sink.send(MlmeRequest::Start(create_start_request(&config)));
        State::Joining { responder, config, pending: PendingRequests::new() }
    } else {
        State::Idle
    }
}

fn validate_config(config: &Config) -> Result<(), JoinMeshResult> {
    let c = Channel::new(config.channel.clone(), Cbw::Cbw20);
    if !c.is_valid() {
        Err(JoinMeshResult::InvalidArguments)
    } else if c.is_dfs() {
        Err(JoinMeshResult::DfsUnsupported)
    } else {
        Ok(())
    }
}

fn create_start_request(config: &Config) -> fidl_mlme::StartRequest {
    fidl_mlme::StartRequest {
        ssid: vec![],
        bss_type: fidl_mlme::BssTypes::Mesh,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: config.channel,
        // TODO(29468): Use actual caps from device here.
        cap: 0,
        // TODO(29468): Use actual rates from device here.
        rates: vec![],
        country: fidl_mlme::Country {
            // TODO(fxbug.dev/29490): Get config from wlancfg
            alpha2: ['U' as u8, 'S' as u8],
            suffix: fidl_mlme::COUNTRY_ENVIRON_ALL,
        },
        rsne: None,
        mesh_id: config.mesh_id.clone(),
        phy: fidl_common::Phy::Ht, // TODO(fxbug.dev/29528, fxbug.dev/29529): Use dynamic value
        cbw: fidl_common::Cbw::Cbw20,
    }
}

fn create_stop_request() -> fidl_mlme::StopRequest {
    fidl_mlme::StopRequest { ssid: vec![] }
}

impl super::Station for MeshSme {
    type Event = ();

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => State::Idle,
            State::Joining { responder, pending, config } => match event {
                MlmeEvent::StartConf { resp } => match resp.result_code {
                    fidl_mlme::StartResultCodes::Success => {
                        responder.respond(JoinMeshResult::Success);
                        if pending.is_empty() {
                            State::Joined { config }
                        } else {
                            // If there are any pending join/leave commands that arrived while we
                            // were waiting for 'Start' to complete, then start leaving immediately,
                            // and then process the pending commands once the 'Stop' call completes.
                            self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                            State::Leaving { config, pending }
                        }
                    }
                    other => {
                        error!("failed to join mesh: {:?}", other);
                        responder.respond(JoinMeshResult::InternalError);
                        on_back_to_idle(pending, &self.mlme_sink)
                    }
                },
                _ => State::Joining { responder, pending, config },
            },
            State::Joined { config } => match event {
                MlmeEvent::IncomingMpOpenAction { action } => {
                    // TODO(gbonik): implement a proper MPM state machine
                    println!("received an MPM Open action: {:?}", action);
                    if mesh_profile_matches(
                        &config.mesh_id,
                        &get_mesh_config(),
                        &action.common.mesh_id,
                        &action.common.mesh_config,
                    ) {
                        let aid = 1;
                        if let Some(params) =
                            create_peering_params(&self.device_info, &config, &action.common, aid)
                        {
                            self.mlme_sink.send(MlmeRequest::MeshPeeringEstablished(params));

                            // TODO(gbonik): actually fill out the data correctly
                            // instead of being a copycat
                            let open = fidl_mlme::MeshPeeringOpenAction {
                                common: fidl_mlme::MeshPeeringCommon {
                                    local_link_id: 0,
                                    ..clone_utils::clone_mesh_peering_common(&action.common)
                                },
                            };
                            self.mlme_sink.send(MlmeRequest::SendMpOpenAction(open));
                            let conf = fidl_mlme::MeshPeeringConfirmAction {
                                common: fidl_mlme::MeshPeeringCommon {
                                    local_link_id: 0,
                                    ..action.common
                                },
                                peer_link_id: action.common.local_link_id,
                                aid,
                            };
                            self.mlme_sink.send(MlmeRequest::SendMpConfirmAction(conf));
                        }
                    }
                    State::Joined { config }
                }
                _ => State::Joined { config },
            },
            State::Leaving { config, pending } => match event {
                MlmeEvent::StopConf { resp } => match resp.result_code {
                    fidl_mlme::StopResultCodes::Success => {
                        on_back_to_idle(pending, &self.mlme_sink)
                    }
                    other => {
                        error!("failed to leave mesh: {:?}", other);
                        for responder in pending.leave {
                            responder.respond(LeaveMeshResult::InternalError);
                        }
                        if let Some((responder, _)) = pending.join {
                            responder.respond(JoinMeshResult::InternalError);
                        }
                        State::Joined { config }
                    }
                },
                _ => State::Leaving { config, pending },
            },
        });
    }

    fn on_timeout(&mut self, _timed_event: TimedEvent<()>) {
        unimplemented!();
    }
}

fn create_peering_params(
    device_info: &DeviceInfo,
    config: &Config,
    peer: &fidl_mlme::MeshPeeringCommon,
    local_aid: Aid,
) -> Option<fidl_mlme::MeshPeeringParams> {
    let band_caps = match get_device_band_info(device_info, config.channel) {
        Some(x) => x,
        None => {
            error!("Failed to find band capabilities for channel {}", config.channel);
            return None;
        }
    };
    let rates = peer.rates.iter().filter(|x| band_caps.rates.contains(x)).cloned().collect();
    Some(fidl_mlme::MeshPeeringParams { peer_sta_address: peer.peer_sta_address, local_aid, rates })
}

impl MeshSme {
    pub fn new(device_info: DeviceInfo) -> (Self, crate::MlmeStream) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let sme =
            MeshSme { mlme_sink: MlmeSink::new(mlme_sink), state: Some(State::Idle), device_info };
        (sme, mlme_stream)
    }
}

fn mesh_profile_matches(
    our_mesh_id: &[u8],
    ours: &fidl_mlme::MeshConfiguration,
    their_mesh_id: &[u8],
    theirs: &fidl_mlme::MeshConfiguration,
) -> bool {
    // IEEE Std 802.11-2016, 14.2.3
    their_mesh_id == our_mesh_id
        && theirs.active_path_sel_proto_id == ours.active_path_sel_proto_id
        && theirs.active_path_sel_metric_id == ours.active_path_sel_metric_id
        && theirs.congest_ctrl_method_id == ours.congest_ctrl_method_id
        && theirs.sync_method_id == ours.sync_method_id
        && theirs.auth_proto_id == ours.auth_proto_id
}

fn get_mesh_config() -> fidl_mlme::MeshConfiguration {
    fidl_mlme::MeshConfiguration {
        active_path_sel_proto_id: 1,  // HWMP
        active_path_sel_metric_id: 1, // Airtime
        congest_ctrl_method_id: 0,    // Inactive
        sync_method_id: 1,            // Neighbor offset sync
        auth_proto_id: 0,             // No auth
        mesh_formation_info: 0,
        mesh_capability: 0x9, // accept additional peerings (0x1) + forwarding (0x8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_validate_config() {
        assert_eq!(
            Err(JoinMeshResult::InvalidArguments),
            validate_config(&Config { mesh_id: vec![], channel: 15 })
        );
        assert_eq!(
            Err(JoinMeshResult::DfsUnsupported),
            validate_config(&Config { mesh_id: vec![], channel: 52 })
        );
        assert_eq!(Ok(()), validate_config(&Config { mesh_id: vec![], channel: 40 }));
    }
}
