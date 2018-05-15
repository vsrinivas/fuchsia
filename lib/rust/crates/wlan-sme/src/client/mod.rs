// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod scan;
mod state;

use fidl_mlme::{self, MlmeEvent, ScanRequest};
use self::scan::{DiscoveryScan, JoinScan, ScanResult, ScanScheduler};
use self::state::State;
use std::collections::VecDeque;
use super::MlmeRequest;
use futures::channel::mpsc;

// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {
    type ScanToken;
}

struct UnboundedSink<T> {
    sink: mpsc::UnboundedSender<T>,
}

impl<T> UnboundedSink<T> {
    fn send(&self, msg: T) {
        match self.sink.unbounded_send(msg) {
            Ok(()) => {},
            Err(e) => {
                if e.is_full() {
                    panic!("Did not expect an unbounded channel to be full: {:?}", e);
                }
                // If the other side has disconnected, we can still technically function,
                // so ignore the error.
            }
        }
    }
}

type MlmeSink = UnboundedSink<MlmeRequest>;
type UserSink<T> = UnboundedSink<UserEvent<T>>;
pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;


pub struct ClientSme<T: Tokens> {
    state: Option<State>,
    scan_sched: ScanScheduler<T::ScanToken>,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
}

// A message from the Client to a user or a group of listeners
pub enum UserEvent<T: Tokens> {
    ScanFinished {
        token: T::ScanToken,
        result: fidl_mlme::ScanConfirm,
    }
}

impl<T: Tokens> ClientSme<T> {
    pub fn new() -> (Self, super::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        (
            ClientSme {
                state: Some(State::Idle),
                scan_sched: ScanScheduler::new(),
                mlme_sink: UnboundedSink{ sink: mlme_sink },
                user_sink: UnboundedSink{ sink: user_sink },
            },
            mlme_stream,
            user_stream
        )
    }

    pub fn on_connect_command(&mut self, ssid: String) {
        let req = self.scan_sched.enqueue_scan_to_join(JoinScan { ssid });
        self.send_scan_request(req);
    }

    pub fn on_scan_command(&mut self, token: T::ScanToken) {
        let req = self.scan_sched.enqueue_scan_to_discover(DiscoveryScan{ token });
        self.send_scan_request(req);
    }

    fn send_scan_request(&mut self, req: Option<ScanRequest>) {
        if let Some(req) = req {
            self.mlme_sink.send(MlmeRequest::Scan(req));
        }
    }
}

impl<T: Tokens> super::Station for ClientSme<T> {
    fn on_mlme_event(&mut self, event: MlmeEvent) {
        self.state = self.state.take().map(|state| match event {
            MlmeEvent::ScanConf{ resp } => {
                let (result, request) = self.scan_sched.on_mlme_scan_confirm(resp);
                self.send_scan_request(request);
                match result {
                    ScanResult::None => state,
                    ScanResult::ReadyToJoin{ best_bss } => {
                        state.disconnect(Some(Box::new(best_bss)), &self.mlme_sink)
                    },
                    ScanResult::CannotJoin(reason) => {
                        eprintln!("Cannot join network because scan failed: {:?}", reason);
                        state
                    },
                    ScanResult::DiscoveryFinished { token, result } => {
                        self.user_sink.send(UserEvent::ScanFinished {
                            token,
                            result
                        });
                        state
                    }
                }
            },
            other => {
                state.on_mlme_event(other, &self.mlme_sink)
            }
        });
    }
}