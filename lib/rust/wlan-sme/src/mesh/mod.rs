// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent},
    futures::channel::mpsc,
    log::{error},
    crate::{
        MlmeRequest,
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
}

mod internal {
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}
use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

enum State<T: Tokens> {
    Idle,
    Joining {
        token: T::JoinToken,
    },
    Joined
}

pub struct MeshSme<T: Tokens> {
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
    state: Option<State<T>>,
}

pub type MeshId = Vec<u8>;

#[derive(Clone, Debug)]
pub struct Config {
    pub mesh_id: MeshId,
    pub channel: u8,
}

#[derive(Clone, Copy, Debug)]
pub enum JoinMeshResult {
    Success,
    Error,
}

// A message from the Mesh node to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    JoinMeshFinished {
        token: T::JoinToken,
        result: JoinMeshResult,
    },
}

impl<T: Tokens> MeshSme<T> {
    pub fn on_join_command(&mut self, token: T::JoinToken, config: Config) {
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => {
                self.mlme_sink.send(MlmeRequest::Start(create_start_request(&config)));
                State::Joining { token }
            },
            s@ State::Joining { .. } | s@ State::Joined => {
                // TODO(gbonik): Leave then re-join
                error!("cannot join mesh because already joined or joining");
                report_join_finished(&self.user_sink, token, JoinMeshResult::Error);
                s
            }
        });
    }
}

fn create_start_request(config: &Config) -> fidl_mlme::StartRequest {
    fidl_mlme::StartRequest {
        ssid: vec![],
        bss_type: fidl_mlme::BssTypes::Mesh,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: config.channel,
        rsne: None,
        mesh_id: config.mesh_id.clone(),
    }
}

impl<T: Tokens> super::Station for MeshSme<T> {
    type Event = ();

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        self.state = Some(match self.state.take().unwrap() {
            State::Idle => State::Idle,
            State::Joining { token } => match event {
                MlmeEvent::StartConf { resp } => match resp.result_code {
                    fidl_mlme::StartResultCodes::Success => {
                        report_join_finished(&self.user_sink, token, JoinMeshResult::Success);
                        State::Joined
                    },
                    other => {
                        error!("failed to join mesh: {:?}", other);
                        report_join_finished(&self.user_sink, token, JoinMeshResult::Error);
                        State::Idle
                    }
                },
                _ => State::Joining { token },
            },
            State::Joined => match event {
                MlmeEvent::IncomingMpOpenAction { action } => {
                    println!("received an MPM Open action: {:?}", action);
                    State::Joined
                },
                _ => State::Joined,
            },
        });
    }

    fn on_timeout(&mut self, _timed_event: TimedEvent<()>) {
        unimplemented!();
    }
}

fn report_join_finished<T: Tokens>(user_sink: &UserSink<T>, token: T::JoinToken,
                                   result: JoinMeshResult)
{
    user_sink.send(UserEvent::JoinMeshFinished { token, result });
}

impl<T: Tokens> MeshSme<T> {
    pub fn new() -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = MeshSme {
            mlme_sink: MlmeSink::new(mlme_sink),
            user_sink: UserSink::new(user_sink),
            state: Some(State::Idle),
        };
        (sme, mlme_stream, user_stream)
    }
}
