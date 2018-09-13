// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bss;
mod rsn;
mod scan;
mod state;

#[cfg(test)]
mod test_utils;

use fidl_fuchsia_wlan_mlme::{MlmeEvent, ScanRequest};
use futures::channel::mpsc;
use log::error;
use std::sync::Arc;

use super::{DeviceInfo, MlmeRequest, MlmeStream, Ssid};

use self::scan::{DiscoveryScan, JoinScan, JoinScanFailure, ScanResult, ScanScheduler};
use self::rsn::get_rsna;
use self::state::{ConnectCommand, State};

use crate::sink::MlmeSink;

pub use self::bss::{BssInfo, EssInfo};
pub use self::scan::{DiscoveryError, DiscoveryResult};

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
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}

use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

pub struct ConnectConfig<T> {
    user_token: T,
    password: Vec<u8>,
}

// An automatically increasing sequence number that uniquely identifies a logical
// connection attempt. Similar to ConnectToken except it is not necessarily tied to a
// particular external command. For example, a new connection attempt can be triggered
// by a DisassociateInd message from the MLME.
pub type ConnectionAttemptId = u64;

pub struct ClientSme<T: Tokens> {
    state: Option<State<T>>,
    scan_sched: ScanScheduler<T::ScanToken, ConnectConfig<T::ConnectToken>>,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
    device_info: Arc<DeviceInfo>,
    att_id: ConnectionAttemptId,
}

#[derive(Debug, PartialEq)]
pub enum ConnectResult {
    Success,
    Canceled,
    Failed,
    BadCredentials,
}

// A message from the Client to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    ScanFinished {
        tokens: Vec<T::ScanToken>,
        result: DiscoveryResult,
    },
    ConnectFinished {
        token: T::ConnectToken,
        result: ConnectResult,
    },
    AssociationStarted {
        att_id: ConnectionAttemptId,
    },
    AssociationSuccess {
        att_id: ConnectionAttemptId,
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Status {
    pub connected_to: Option<BssInfo>,
    pub connecting_to: Option<Ssid>
}

impl<T: Tokens> ClientSme<T> {
    pub fn new(info: DeviceInfo) -> (Self, MlmeStream, UserStream<T>) {
        let device_info = Arc::new(info);
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        (
            ClientSme {
                state: Some(State::Idle),
                scan_sched: ScanScheduler::new(Arc::clone(&device_info)),
                mlme_sink: MlmeSink::new(mlme_sink),
                user_sink: UserSink::new(user_sink),
                device_info,
                att_id: 0,
            },
            mlme_stream,
            user_stream
        )
    }

    pub fn on_connect_command(&mut self, ssid: Ssid, password: Vec<u8>, token: T::ConnectToken) {
        let (canceled_token, req) = self.scan_sched.enqueue_scan_to_join(
            JoinScan {
                ssid,
                token: ConnectConfig { user_token: token, password }
            });
        // If the new scan replaced an existing pending JoinScan, notify the existing transaction
        if let Some(t) = canceled_token {
            self.user_sink.send(UserEvent::ConnectFinished {
                token: t.user_token,
                result: ConnectResult::Canceled
            });
        }
        self.send_scan_request(req);
    }

    pub fn on_disconnect_command(&mut self) {
        self.state = self.state.take().map(
            |state| state.disconnect(&self.mlme_sink, &self.user_sink));
    }

    pub fn on_scan_command(&mut self, token: T::ScanToken) {
        let req = self.scan_sched.enqueue_scan_to_discover(DiscoveryScan{ token });
        self.send_scan_request(req);
    }

    pub fn status(&self) -> Status {
        let status = self.state.as_ref().expect("expected state to be always present").status();
        if status.connecting_to.is_some() {
            status
        } else {
            // If the association machine is not connecting to a network, but the scanner
            // has a queued 'JoinScan', include the SSID we are trying to connect to
            Status {
                connecting_to: self.scan_sched.get_join_scan().map(|s| s.ssid.clone()),
                .. status
            }
        }
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
            MlmeEvent::OnScanResult { result } => {
                self.scan_sched.on_mlme_scan_result(result);
                state
            },
            MlmeEvent::OnScanEnd { end } => {
                let (result, request) = self.scan_sched.on_mlme_scan_end(end);
                self.send_scan_request(request);
                match result {
                    ScanResult::None => state,
                    ScanResult::ReadyToJoin { token, best_bss } => {
                        match get_rsna(&self.device_info, &token.password, &best_bss) {
                            Ok(rsna) => {
                                let cmd = ConnectCommand {
                                    bss: Box::new(best_bss),
                                    token: Some(token.user_token),
                                    rsna
                                };
                                state.connect(cmd, &self.mlme_sink, &self.user_sink, &mut self.att_id)
                            },
                            Err(err) => {
                                error!("cannot join BSS {:?} {:?}", best_bss.bssid, err);
                                self.user_sink.send(UserEvent::ConnectFinished {
                                    token: token.user_token,
                                    result: ConnectResult::Failed,
                                });
                                state
                            }
                        }
                    },
                    ScanResult::CannotJoin { token, reason } => {
                        error!("cannot join network because scan failed: {:?}", reason);
                        self.user_sink.send(UserEvent::ConnectFinished {
                            token: token.user_token,
                            result: match reason {
                                JoinScanFailure::Canceled => ConnectResult::Canceled,
                                _ => ConnectResult::Failed
                            }
                        });
                        state
                    },
                    ScanResult::DiscoveryFinished { tokens, result } => {
                        self.user_sink.send(UserEvent::ScanFinished {
                            tokens,
                            result
                        });
                        state
                    }
                }
            },
            other => {
                state.on_mlme_event(&self.device_info, other, &self.mlme_sink, &self.user_sink, &mut self.att_id)
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use std::collections::HashSet;
    use std::error::Error;

    use super::test_utils::{fake_protected_bss_description, fake_unprotected_bss_description};
    use super::UserEvent::ConnectFinished;

    use crate::Station;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];

    #[test]
    fn status_connecting_to() {
        let (mut sme, _mlme_stream, _user_stream) = create_sme();
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        // Issue a connect command and expect the status to change appropriately.
        // We also check that the association machine state is still disconnected
        // to make sure that the status comes from the scanner.
        sme.on_connect_command(b"foo".to_vec(), vec![], 10);
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        // Push a fake scan result into SME. We should still be connecting to "foo",
        // but the status should now come from the state machine and not from the scanner.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fake_unprotected_bss_description(b"foo".to_vec()),
            }
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd {
                txn_id: 1,
                code: fidl_mlme::ScanResultCodes::Success,
            }
        });
        assert_eq!(Some(b"foo".to_vec()),
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        // Even if we scheduled a scan to connect to another network "bar", we should
        // still report that we are connecting to "foo".
        sme.on_connect_command(b"bar".to_vec(), vec![], 10);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        // Simulate that joining "foo" failed. We should now be connecting to "bar".
        sme.on_mlme_event(MlmeEvent::JoinConf {
            resp: fidl_mlme::JoinConfirm {
                result_code: fidl_mlme::JoinResultCodes::JoinFailureTimeout,
            }
        });
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"bar".to_vec()) },
                   sme.status());
    }

    #[test]
    fn connecting_password_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _user_stream) = create_sme();
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        // Issue a connect command and verify that connecting_to status is changed for upper
        // layer (but not underlying state machine) and a scan request is sent to MLME.
        sme.on_connect_command(b"foo".to_vec(), "somepass".as_bytes().to_vec(), 10);
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        if let Ok(Some(MlmeRequest::Scan(..))) = mlme_stream.try_next() {
            // expected path; nothing to do
        } else {
            panic!("expect scan request to MLME");
        }

        // Simulate scan end and verify that underlying state machine's status is changed,
        // and a join request is sent to MLME.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fake_protected_bss_description(b"foo".to_vec()),
            }
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd {
                txn_id: 1,
                code: fidl_mlme::ScanResultCodes::Success,
            }
        });
        assert_eq!(Some(b"foo".to_vec()),
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        if let Ok(Some(MlmeRequest::Join(..))) = mlme_stream.try_next() {
            // expected path; nothing to do
        } else {
            panic!("expect join request to MLME");
        }
    }

    #[test]
    fn connecting_password_supplied_for_unprotected_network() {
        let (mut sme, mut mlme_stream, mut user_stream) = create_sme();
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        sme.on_connect_command(b"foo".to_vec(), "somepass".as_bytes().to_vec(), 10);
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        // Push a fake scan result into SME. We should not attempt to connect
        // because a password was supplied for unprotected network. So both the
        // SME client and underlying state machine should report not connecting
        // anymore.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fake_unprotected_bss_description(b"foo".to_vec()),
            }
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd {
                txn_id: 1,
                code: fidl_mlme::ScanResultCodes::Success,
            }
        });
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        // No join request should be sent to MLME
        loop {
            match mlme_stream.try_next() {
                Ok(event) => match event {
                    Some(MlmeRequest::Join(..)) => panic!("unexpected join request to MLME"),
                    None => break,
                    _ => (),
                }
                Err(e) => {
                    assert_eq!(e.description(), "receiver channel is empty");
                    break;
                }
            }
        }

        // User should get a message that connection failed
        let user_event = user_stream.try_next().unwrap().expect("expect message for user");
        if let ConnectFinished { result, .. } = user_event {
            assert_eq!(result, ConnectResult::Failed);
        } else {
            panic!("unexpected user event type in sent message");
        }
    }

    #[test]
    fn connecting_no_password_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, mut user_stream) = create_sme();
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        sme.on_connect_command(b"foo".to_vec(), vec![], 10);
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: Some(b"foo".to_vec()) },
                   sme.status());

        // Push a fake scan result into SME. We should not attempt to connect
        // because no password was supplied for a protected network.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fake_protected_bss_description(b"foo".to_vec()),
            }
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd {
                txn_id: 1,
                code: fidl_mlme::ScanResultCodes::Success,
            }
        });
        assert_eq!(None,
                   sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status{ connected_to: None, connecting_to: None },
                   sme.status());

        // No join request should be sent to MLME
        loop {
            match mlme_stream.try_next() {
                Ok(event) => match event {
                    Some(MlmeRequest::Join(..)) => panic!("unexpected join request sent to MLME"),
                    None => break,
                    _ => (),
                }
                Err(e) => {
                    assert_eq!(e.description(), "receiver channel is empty");
                    break;
                }
            }
        }

        // User should get a message that connection failed
        let user_event = user_stream.try_next().unwrap().expect("expect message for user");
        if let ConnectFinished { result, .. } = user_event {
            assert_eq!(result, ConnectResult::Failed);
        } else {
            panic!("unexpected user event type in sent message");
        }
    }

    struct FakeTokens;
    impl Tokens for FakeTokens {
        type ScanToken = i32;
        type ConnectToken = i32;
    }

    fn create_sme() -> (ClientSme<FakeTokens>, MlmeStream, UserStream<FakeTokens>) {
        ClientSme::new(DeviceInfo {
            supported_channels: HashSet::new(),
            addr: CLIENT_ADDR,
        })
    }
}
