// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod scan;
mod state;

use fidl_mlme::{MlmeEvent, ScanRequest};
use self::scan::{DiscoveryScan, JoinScan, JoinScanFailure, ScanResult, ScanScheduler};
use self::state::{ConnectCommand, State};
use std::sync::Arc;
use super::{DeviceCapabilities, MlmeRequest};
use futures::channel::mpsc;

pub use self::scan::{DiscoveryError, DiscoveryResult, DiscoveredEss};

// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {
    type ScanToken;
    type ConnectToken;
}

// This is necessary to trick the private-in-public checker.
// A private module is not allowed to include private types in its interface,
// even though the module itself is private and will never be exported.
// As a workaround, we add another private module with public types.
mod internal {
    use futures::channel::mpsc;
    use super::UserEvent;

    pub struct UnboundedSink<T> {
        pub sink: mpsc::UnboundedSender<T>,
    }

    impl<T> UnboundedSink<T> {
        pub fn send(&self, msg: T) {
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

    pub type MlmeSink = UnboundedSink<super::super::MlmeRequest>;
    pub type UserSink<T> = UnboundedSink<UserEvent<T>>;
}

use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

pub struct ClientSme<T: Tokens> {
    state: Option<State<T>>,
    scan_sched: ScanScheduler<T::ScanToken, T::ConnectToken>,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
}

pub enum ConnectResult {
    Success,
    Canceled,
    Failed
}

// A message from the Client to a user or a group of listeners
pub enum UserEvent<T: Tokens> {
    ScanFinished {
        token: T::ScanToken,
        result: DiscoveryResult,
    },
    ConnectFinished {
        token: T::ConnectToken,
        result: ConnectResult
    }
}

impl<T: Tokens> ClientSme<T> {
    pub fn new(caps: DeviceCapabilities) -> (Self, super::MlmeStream, UserStream<T>) {
        let caps = Arc::new(caps);
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        (
            ClientSme {
                state: Some(State::Idle),
                scan_sched: ScanScheduler::new(caps),
                mlme_sink: UnboundedSink{ sink: mlme_sink },
                user_sink: UnboundedSink{ sink: user_sink },
            },
            mlme_stream,
            user_stream
        )
    }

    pub fn on_connect_command(&mut self, ssid: Vec<u8>, token: T::ConnectToken) {
        let (canceled_token, req) = self.scan_sched.enqueue_scan_to_join(JoinScan { ssid, token });
        // If the new scan replaced an existing pending JoinScan, notify the existing transaction
        if let Some(t) = canceled_token {
            self.user_sink.send(UserEvent::ConnectFinished {
                token: t,
                result: ConnectResult::Canceled
            });
        }
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
                    ScanResult::ReadyToJoin { token, best_bss } => {
                        let cmd = ConnectCommand { bss: Box::new(best_bss), token: Some(token) };
                        state.disconnect(Some(cmd), &self.mlme_sink, &self.user_sink)
                    },
                    ScanResult::CannotJoin { token, reason } => {
                        eprintln!("Cannot join network because scan failed: {:?}", reason);
                        self.user_sink.send(UserEvent::ConnectFinished {
                            token,
                            result: match reason {
                                JoinScanFailure::Canceled => ConnectResult::Canceled,
                                _ => ConnectResult::Failed
                            }
                        });
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
                state.on_mlme_event(other, &self.mlme_sink, &self.user_sink)
            }
        });
    }
}