// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common::{self as fidl_common},
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent},
    futures::channel::mpsc,
    log::{error},
    std::mem,
    wlan_common::channel::{Channel, Cbw},
    crate::{
        clone_utils,
        DeviceInfo,
        MlmeRequest,
        phy_selection::get_device_band_info,
        sink::MlmeSink,
        timer::TimedEvent,
    },
};

const DEFAULT_BEACON_PERIOD: u16 = 1000;
const DEFAULT_DTIM_PERIOD: u8 = 1;


// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {
    type JoinToken;
    type LeaveToken;
}

mod internal {
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}
use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

// A list of pending join/leave requests to be maintained in the intermediate
// 'Joining'/'Leaving' states where we are waiting for a reply from MLME and cannot
// serve the requests immediately.
struct PendingRequests<T: Tokens> {
    leave: Vec<T::LeaveToken>,
    join: Option<(T::JoinToken, Config)>,
}

impl<T: Tokens> PendingRequests<T> {
    pub fn new() -> Self {
        PendingRequests { leave: Vec::new(), join: None }
    }

    pub fn enqueue_leave(&mut self, user_sink: &UserSink<T>, token: T::LeaveToken) {
        self.replace_join_request(user_sink, None);
        self.leave.push(token);
    }

    pub fn enqueue_join(&mut self, user_sink: &UserSink<T>, token: T::JoinToken, config: Config) {
        self.replace_join_request(user_sink, Some((token, config)));
    }

    pub fn is_empty(&self) -> bool {
        self.leave.is_empty() && self.join.is_none()
    }

    fn replace_join_request(
        &mut self,
        user_sink: &UserSink<T>,
        req: Option<(T::JoinToken, Config)>)
    {
        if let Some((old_token, _)) = mem::replace(&mut self.join, req) {
            report_join_finished(user_sink, old_token, JoinMeshResult::Canceled);
        }
    }
}

enum State<T: Tokens> {
    Idle,
    Joining {
        token: T::JoinToken,
        config: Config,
        pending: PendingRequests<T>,
    },
    Joined {
        config: Config,
    },
    Leaving {
        config: Config,
        pending: PendingRequests<T>,
    }
}

pub struct MeshSme<T: Tokens> {
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
    state: Option<State<T>>,
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

// A message from the Mesh node to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    JoinMeshFinished {
        token: T::JoinToken,
        result: JoinMeshResult,
    },
    LeaveMeshFinished {
        token: T::LeaveToken,
        result: LeaveMeshResult,
    }
}

impl<T: Tokens> MeshSme<T> {
    pub fn on_join_command(&mut self, token: T::JoinToken, config: Config) {
        if let Err(result) = validate_config(&config) {
            report_join_finished(&self.user_sink, token, result);
            return;
        }
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => {
                self.mlme_sink.send(MlmeRequest::Start(create_start_request(&config)));
                State::Joining { token, pending: PendingRequests::new(), config }
            },
            State::Joining { token: cur_token, config: cur_config, mut pending } => {
                pending.enqueue_join(&self.user_sink, token, config);
                State::Joining { token: cur_token, config: cur_config, pending }
            },
            State::Joined { config: cur_config } => {
                self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                let mut pending = PendingRequests::new();
                pending.enqueue_join(&self.user_sink, token, config);
                State::Leaving { config: cur_config, pending }
            },
            State::Leaving { config: cur_config, mut pending } => {
                pending.enqueue_join(&self.user_sink, token, config);
                State::Leaving { config: cur_config, pending }
            }
        });
    }

    pub fn on_leave_command(&mut self, token: T::LeaveToken) {
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => {
                report_leave_finished(&self.user_sink, token, LeaveMeshResult::Success);
                State::Idle
            },
            State::Joining { token: cur_token, config, mut pending } => {
                pending.enqueue_leave(&self.user_sink, token);
                State::Joining { token: cur_token, pending, config }
            },
            State::Joined { config } => {
                self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                let mut pending = PendingRequests::new();
                pending.enqueue_leave(&self.user_sink, token);
                State::Leaving { config, pending }
            },
            State::Leaving { config, mut pending } => {
                pending.enqueue_leave(&self.user_sink, token);
                State::Leaving { config, pending }
            }
        });
    }
}

fn on_back_to_idle<T: Tokens>(
    pending: PendingRequests<T>,
    user_sink: &UserSink<T>,
    mlme_sink: &MlmeSink
) -> State<T> {
    for token in pending.leave {
        report_leave_finished(user_sink, token, LeaveMeshResult::Success);
    }
    if let Some((token, config)) = pending.join {
        mlme_sink.send(MlmeRequest::Start(create_start_request(&config)));
        State::Joining { token, config, pending: PendingRequests::new() }
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
        country: fidl_mlme::Country { // TODO(WLAN-870): Get config from wlancfg
            alpha2: ['U' as u8, 'S' as u8],
            suffix: fidl_mlme::COUNTRY_ENVIRON_ALL,
        },
        rsne: None,
        mesh_id: config.mesh_id.clone(),
        phy: fidl_common::Phy::Ht,  // TODO(WLAN-908, WLAN-909): Use dynamic value
        cbw: fidl_common::Cbw::Cbw20,

    }
}

fn create_stop_request() -> fidl_mlme::StopRequest {
    fidl_mlme::StopRequest { ssid: vec![], }
}

impl<T: Tokens> super::Station for MeshSme<T> {
    type Event = ();

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => State::Idle,
            State::Joining { token, pending, config } => match event {
                MlmeEvent::StartConf { resp } => match resp.result_code {
                    fidl_mlme::StartResultCodes::Success => {
                        report_join_finished(&self.user_sink, token, JoinMeshResult::Success);
                        if pending.is_empty() {
                            State::Joined { config }
                        } else {
                            // If there are any pending join/leave commands that arrived while we
                            // were waiting for 'Start' to complete, then start leaving immediately,
                            // and then process the pending commands once the 'Stop' call completes.
                            self.mlme_sink.send(MlmeRequest::Stop(create_stop_request()));
                            State::Leaving { config, pending }
                        }
                    },
                    other => {
                        error!("failed to join mesh: {:?}", other);
                        report_join_finished(&self.user_sink, token, JoinMeshResult::InternalError);
                        on_back_to_idle(pending, &self.user_sink, &self.mlme_sink)
                    }
                },
                _ => State::Joining { token, pending, config },
            },
            State::Joined { config } => match event {
                MlmeEvent::IncomingMpOpenAction { action } => {
                    // TODO(gbonik): implement a proper MPM state machine
                    println!("received an MPM Open action: {:?}", action);
                    if mesh_profile_matches(&config.mesh_id, &get_mesh_config(),
                                            &action.common.mesh_id, &action.common.mesh_config) {
                        let aid = 1;
                        if let Some(params) = create_peering_params(
                                    &self.device_info, &config, &action.common, aid)
                        {
                            self.mlme_sink.send(MlmeRequest::MeshPeeringEstablished(params));

                            // TODO(gbonik): actually fill out the data correctly
                            // instead of being a copycat
                            let open = fidl_mlme::MeshPeeringOpenAction {
                                common: fidl_mlme::MeshPeeringCommon {
                                    local_link_id: 0,
                                    .. clone_utils::clone_mesh_peering_common(&action.common)
                                },
                            };
                            self.mlme_sink.send(MlmeRequest::SendMpOpenAction(open));
                            let conf = fidl_mlme::MeshPeeringConfirmAction {
                                common: fidl_mlme::MeshPeeringCommon {
                                    local_link_id: 0,
                                    .. action.common
                                },
                                peer_link_id: action.common.local_link_id,
                                aid,
                            };
                            self.mlme_sink.send(MlmeRequest::SendMpConfirmAction(conf));
                        }
                    }
                    State::Joined { config }
                },
                _ => State::Joined { config },
            },
            State::Leaving { config, pending } => match event {
                MlmeEvent::StopConf { resp } => match resp.result_code {
                    fidl_mlme::StopResultCodes::Success =>
                        on_back_to_idle(pending, &self.user_sink, &self.mlme_sink),
                    other => {
                        error!("failed to leave mesh: {:?}", other);
                        for token in pending.leave {
                            report_leave_finished(
                                    &self.user_sink, token, LeaveMeshResult::InternalError);
                        }
                        if let Some((token, _)) = pending.join {
                            report_join_finished(
                                    &self.user_sink, token, JoinMeshResult::InternalError);
                        }
                        State::Joined { config }
                    }
                },
                _ => State::Leaving { config, pending }
            }
        });
    }

    fn on_timeout(&mut self, _timed_event: TimedEvent<()>) {
        unimplemented!();
    }
}

fn create_peering_params(device_info: &DeviceInfo,
                         config: &Config,
                         peer: &fidl_mlme::MeshPeeringCommon,
                         local_aid: u16)
    -> Option<fidl_mlme::MeshPeeringParams>
{
    let band_caps = match get_device_band_info(device_info, config.channel) {
        Some(x) => x,
        None => {
            error!("Failed to find band capabilities for channel {}", config.channel);
            return None;
        }
    };
    let rates = peer.rates.iter().filter(|x| band_caps.basic_rates.contains(x)).cloned().collect();
    Some(fidl_mlme::MeshPeeringParams {
        peer_sta_address: peer.peer_sta_address,
        local_aid,
        rates
    })
}

fn report_join_finished<T: Tokens>(user_sink: &UserSink<T>,
                                   token: T::JoinToken,
                                   result: JoinMeshResult)
{
    user_sink.send(UserEvent::JoinMeshFinished { token, result });
}

fn report_leave_finished<T: Tokens>(user_sink: &UserSink<T>, token: T::LeaveToken,
                                    result: LeaveMeshResult)
{
    user_sink.send(UserEvent::LeaveMeshFinished { token, result });
}

impl<T: Tokens> MeshSme<T> {
    pub fn new(device_info: DeviceInfo) -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = MeshSme {
            mlme_sink: MlmeSink::new(mlme_sink),
            user_sink: UserSink::new(user_sink),
            state: Some(State::Idle),
            device_info,
        };
        (sme, mlme_stream, user_stream)
    }
}

fn mesh_profile_matches(our_mesh_id: &[u8],
                        ours: &fidl_mlme::MeshConfiguration,
                        their_mesh_id: &[u8],
                        theirs: &fidl_mlme::MeshConfiguration) -> bool {
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
        active_path_sel_proto_id: 1, // HWMP
        active_path_sel_metric_id: 1, // Airtime
        congest_ctrl_method_id: 0, // Inactive
        sync_method_id: 1, // Neighbor offset sync
        auth_proto_id: 0, // No auth
        mesh_formation_info: 0,
        mesh_capability: 0x9, // accept additional peerings (0x1) + forwarding (0x8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_validate_config() {
        assert_eq!(Err(JoinMeshResult::InvalidArguments),
                   validate_config(&Config { mesh_id: vec![], channel: 15}));
        assert_eq!(Err(JoinMeshResult::DfsUnsupported),
                   validate_config(&Config { mesh_id: vec![], channel: 52}));
        assert_eq!(Ok(()),
                   validate_config(&Config { mesh_id: vec![], channel: 40}));
    }

}