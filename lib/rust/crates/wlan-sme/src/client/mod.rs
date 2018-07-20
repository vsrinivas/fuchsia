// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bss;
mod scan;
mod state;

#[cfg(test)]
mod test_utils;

use bytes::Bytes;
use fidl_mlme::{MlmeEvent, ScanRequest, BssDescription};
use self::scan::{DiscoveryScan, JoinScan, JoinScanFailure, ScanResult, ScanScheduler};
use self::state::{ConnectCommand, Rsna, State};
use std::sync::Arc;
use super::{DeviceInfo, MlmeRequest, MlmeStream, Ssid};
use wlan_rsn::{akm, auth, cipher, rsne::{self, Rsne}, suite_selector::OUI};
use wlan_rsn::key::exchange;
use wlan_rsn::rsna::{esssa::EssSa, Role};
use failure::Error;
use futures::channel::mpsc;

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

pub struct ConnectConfig<T> {
    user_token: T,
    password: Vec<u8>,
}

pub struct ClientSme<T: Tokens> {
    state: Option<State<T>>,
    scan_sched: ScanScheduler<T::ScanToken, ConnectConfig<T::ConnectToken>>,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
    device_info: Arc<DeviceInfo>,
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
                mlme_sink: UnboundedSink{ sink: mlme_sink },
                user_sink: UnboundedSink{ sink: user_sink },
                device_info,
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

    fn get_connect_command(&self, token: ConnectConfig<T::ConnectToken>, bss: BssDescription)
        -> Option<ConnectCommand<T::ConnectToken>>
    {
        let a_rsne = bss.rsn.as_ref().and_then(|a_rsne| {
            let r = rsne::from_bytes(&a_rsne[..]).to_full_result();
            if let Err(e) = &r {
                eprintln!("BSS {:?} carries invalid RSNE: {:?}; error: {:?}",
                          bss.bssid, &a_rsne[..], e);
            }
            r.ok()
        });

        match a_rsne {
            None if bss.rsn.is_some() => {
                eprintln!("cannot join BSS {:?} invalid RSNE", bss.bssid);
                None
            },
            None => Some(ConnectCommand {
                bss: Box::new(bss),
                token: Some(token.user_token),
                rsna: None
            }),
            Some(a_rsne) => match derive_s_rsne(&a_rsne) {
                None => {
                    eprintln!("cannot join BSS {:?} unsupported RSNE {:?}", bss.bssid, a_rsne);
                    None
                },
                Some(s_rsne) => {
                    let ssid = bss.ssid.clone();
                    match make_esssa(ssid.as_bytes(),
                                     &token.password[..],
                                     self.device_info.addr,
                                     s_rsne.clone(),
                                     bss.bssid,
                                     a_rsne) {
                        Ok(esssa) => {
                            let rsna =  Some(Rsna::new(s_rsne, esssa));
                            Some(ConnectCommand {
                                bss: Box::new(bss),
                                token: Some(token.user_token),
                                rsna
                            })
                        },
                        Err(e) => {
                            eprintln!("cannot join BSS {:?} error creating ESS-SA: {:?}",
                                      bss.bssid, e);
                            None
                        },
                    }
                }
            }
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
                        let cmd = self.get_connect_command(token, best_bss);
                        match cmd {
                            None => state,
                            some_cmd => {
                                state.disconnect(some_cmd, &self.mlme_sink, &self.user_sink)
                            }
                        }
                    },
                    ScanResult::CannotJoin { token, reason } => {
                        eprintln!("Cannot join network because scan failed: {:?}", reason);
                        self.user_sink.send(UserEvent::ConnectFinished {
                            token: token.user_token,
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
                state.on_mlme_event(&self.device_info, other, &self.mlme_sink, &self.user_sink)
            }
        });
    }
}

fn make_esssa(ssid: &[u8], passphrase: &[u8], sta_addr: [u8; 6], sta_rsne: Rsne, bssid: [u8; 6], bss_rsne: Rsne)
    -> Result<EssSa, Error>
{
    let auth_cfg = auth::Config::for_psk(passphrase, ssid)?;
    let ptk_cfg = exchange::Config::for_4way_handshake(Role::Supplicant, sta_addr, sta_rsne, bssid, bss_rsne)?;
    EssSa::new(Role::Supplicant, auth_cfg, ptk_cfg)
}

fn derive_s_rsne(a_rsne: &Rsne) -> Option<Rsne>
{
    // Supported Ciphers and AKMs:
    // Group: CCMP-128, TKIP
    // Pairwise: CCMP-128
    // AKMS: PSK
    let has_supported_group_data_cipher = match a_rsne.group_data_cipher_suite.as_ref() {
        Some(c) if c.has_known_usage() => match c.suite_type {
            // IEEE allows TKIP usage only in GTKSAs for compatibility reasons.
            // TKIP is considered broken and should never be used in a PTKSA or IGTKSA.
            cipher::CCMP_128 | cipher::TKIP => true,
            _ => false,
        },
        _ => false,
    };
    let has_supported_pairwise_cipher = a_rsne.pairwise_cipher_suites.iter()
        .any(|c| c.has_known_usage() && c.suite_type == cipher::CCMP_128);
    let has_supported_akm_suite = a_rsne.akm_suites.iter()
        .any(|a| a.has_known_algorithm() && a.suite_type == akm::PSK);
    if !has_supported_group_data_cipher || !has_supported_pairwise_cipher || !has_supported_akm_suite {
        return None;
    }

    // If Authenticator's RSNE is supported, construct Supplicant's RSNE.
    let mut s_rsne = Rsne::new();
    s_rsne.group_data_cipher_suite = a_rsne.group_data_cipher_suite.clone();
    let pairwise_cipher =
        cipher::Cipher{oui: Bytes::from(&OUI[..]), suite_type: cipher::CCMP_128 };
    s_rsne.pairwise_cipher_suites.push(pairwise_cipher);
    let akm = akm::Akm{oui: Bytes::from(&OUI[..]), suite_type: akm::PSK };
    s_rsne.akm_suites.push(akm);
    Some(s_rsne)
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::test_utils::fake_bss_description;
    use fidl_mlme;
    use std::collections::HashSet;
    use Station;

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
                bss: fake_bss_description(b"foo".to_vec()),
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

    fn make_cipher(suite_type: u8) -> cipher::Cipher {
        cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
    }

    fn make_akm(suite_type: u8) -> akm::Akm {
        akm::Akm { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
    }

    fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
        let a_rsne = Rsne {
            version: 1,
            group_data_cipher_suite: data.map(|t| make_cipher(t)),
            pairwise_cipher_suites: pairwise.into_iter().map(|t| make_cipher(t)).collect(),
            akm_suites: akms.into_iter().map(|t| make_akm(t)).collect(),
            ..Default::default()
        };
        a_rsne
    }

    fn rsne_as_bytes(s_rsne: Rsne) -> Vec<u8> {
        let mut buf = Vec::with_capacity(s_rsne.len());
        s_rsne.as_bytes(&mut buf);
        buf
    }

    #[test]
    fn test_incompatible_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_incompatible_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::BIP_CMAC_256], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_tkip_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::TKIP], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_tkip_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::TKIP), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 2, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_ccmp128_group_data_pairwise_cipher_psk() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_mixed_mode() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128, cipher::TKIP], vec![akm::PSK, akm::FT_PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_no_group_data_cipher() {
        let a_rsne = make_rsne(None, vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_no_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_no_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_incompatible_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP]);
        let s_rsne = derive_s_rsne(&a_rsne);
        assert_eq!(s_rsne, None);
    }
}