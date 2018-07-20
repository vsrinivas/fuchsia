// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use client::bss::convert_bss_description;
use fidl_mlme::{self, BssDescription, MlmeEvent};
use client::{ConnectResult, Status, Tokens};
use client::internal::{MlmeSink, UserSink};
use MlmeRequest;
use super::DeviceInfo;
use wlan_rsn::{akm, cipher, rsna::esssa::EssSa, rsne, rsne::Rsne, suite_selector::OUI};

const DEFAULT_JOIN_FAILURE_TIMEOUT: u32 = 20; // beacon intervals
const DEFAULT_AUTH_FAILURE_TIMEOUT: u32 = 20; // beacon intervals

pub enum LinkState {
    _EstablishingRsna,
    LinkUp
}

pub struct ConnectCommand<T> {
    pub bss: Box<BssDescription>,
    pub token: Option<T>
}

pub struct Rsna {
    _s_rsne: Rsne,
    _esssa: EssSa,
}

impl Rsna {
    fn _new(s_rsne: Rsne, esssa: EssSa) -> Rsna {
        assert_eq!(s_rsne.pairwise_cipher_suites.len(), 1);
        assert_eq!(s_rsne.akm_suites.len(), 1);
        assert!(s_rsne.group_data_cipher_suite.is_none());

        Rsna {_s_rsne: s_rsne, _esssa: esssa}
    }
}

pub enum State<T: Tokens> {
    Idle,
    Joining {
        cmd: ConnectCommand<T::ConnectToken>,
    },
    Authenticating {
        cmd: ConnectCommand<T::ConnectToken>,
    },
    Associating {
        cmd: ConnectCommand<T::ConnectToken>,
        s_rsne: Option<Rsne>,
    },
    Associated {
        bss: Box<BssDescription>,
        last_rssi: Option<i8>,
        link_state: LinkState,
        rsna: Option<Rsna>,
    },
    Deauthenticating {
        // Network to join after the deauthentication process is finished
        next_cmd: Option<ConnectCommand<T::ConnectToken>>,
    }
}

impl<T: Tokens> State<T> {
    pub fn on_mlme_event(self, _device_info: &DeviceInfo, event: MlmeEvent, mlme_sink: &MlmeSink,
                         user_sink: &UserSink<T>) -> Self {
        match self {
            State::Idle => {
                eprintln!("Unexpected MLME message while Idle: {:?}", event);
                State::Idle
            },
            State::Joining{ cmd } => match event {
                MlmeEvent::JoinConf { resp } => match resp.result_code {
                    fidl_mlme::JoinResultCodes::Success => {
                        mlme_sink.send(MlmeRequest::Authenticate(
                            fidl_mlme::AuthenticateRequest {
                                peer_sta_address: cmd.bss.bssid.clone(),
                                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                                auth_failure_timeout: DEFAULT_AUTH_FAILURE_TIMEOUT,
                            }));
                        State::Authenticating { cmd }
                    },
                    other => {
                        eprintln!("Join request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => {
                    State::Joining{ cmd }
                }
            },
            State::Authenticating{ cmd } => match event {
                MlmeEvent::AuthenticateConf { resp } => match resp.result_code {
                    fidl_mlme::AuthenticateResultCodes::Success => {
                        to_associating_state(cmd, mlme_sink)
                    },
                    other => {
                        eprintln!("Authenticate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => State::Authenticating{ cmd }
            },
            State::Associating{ cmd, s_rsne } => match event {
                MlmeEvent::AssociateConf { resp } => match resp.result_code {
                    fidl_mlme::AssociateResultCodes::Success => {
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Success);
                        State::Associated {
                            bss: cmd.bss,
                            last_rssi: None,
                            // TODO(hahnr): Construct RSNA and adjust LinkState when connecting to
                            // a protected BSS.
                            link_state: LinkState::LinkUp,
                            rsna: None,
                        }
                    },
                    other => {
                        eprintln!("Associate request failed with result code {:?}", other);
                        report_connect_finished(cmd.token, user_sink, ConnectResult::Failed);
                        State::Idle
                    }
                },
                _ => State::Associating{ cmd, s_rsne }
            },
            State::Associated { bss, last_rssi, link_state, rsna } => match event {
                MlmeEvent::DisassociateInd{ .. } => {
                    let cmd = ConnectCommand{ bss, token: None };
                    to_associating_state(cmd, mlme_sink)
                },
                MlmeEvent::DeauthenticateInd{ .. } => {
                    State::Idle
                },
                MlmeEvent::SignalReport{ ind } => {
                    State::Associated {
                        bss,
                        last_rssi: Some(ind.rssi_dbm),
                        link_state,
                        rsna
                    }
                },
                _ => State::Associated{ bss, last_rssi, link_state, rsna }
            },
            State::Deauthenticating{ next_cmd } => match event {
                MlmeEvent::DeauthenticateConf{ .. } => {
                    disconnect_or_join(next_cmd, mlme_sink)
                },
                _ => State::Deauthenticating { next_cmd }
            },
        }
    }

    pub fn disconnect(self, next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                      mlme_sink: &MlmeSink, user_sink: &UserSink<T>) -> Self {
        match self {
            State::Idle => {
                disconnect_or_join(next_bss_to_join, mlme_sink)
            },
            State::Joining { cmd } | State::Authenticating { cmd }  => {
                report_connect_finished(cmd.token, user_sink, ConnectResult::Canceled);
                disconnect_or_join(next_bss_to_join, mlme_sink)
            },
            State::Associating{ cmd, .. } => {
                report_connect_finished(cmd.token, user_sink, ConnectResult::Canceled);
                to_deauthenticating_state(cmd.bss, next_bss_to_join, mlme_sink)
            },
            State::Associated { bss, .. } => {
                to_deauthenticating_state(bss, next_bss_to_join, mlme_sink)
            },
            State::Deauthenticating { next_cmd } => {
                if let Some(next_cmd) = next_cmd {
                    report_connect_finished(next_cmd.token, user_sink, ConnectResult::Canceled);
                }
                State::Deauthenticating {
                    next_cmd: next_bss_to_join
                }
            }
        }
    }

    pub fn status(&self) -> Status {
        match self {
            State::Idle | State::Deauthenticating { next_cmd: None } => Status {
                connected_to: None,
                connecting_to: None,
            },
            State::Joining { cmd }
                | State::Authenticating { cmd }
                | State::Associating { cmd, .. }
                | State::Deauthenticating { next_cmd: Some(cmd) }  =>
            {
                Status {
                    connected_to: None,
                    connecting_to: Some(cmd.bss.ssid.as_bytes().to_vec()),
                }
            },
            State::Associated { bss, link_state: LinkState::_EstablishingRsna, .. } => Status {
                connected_to: None,
                connecting_to: Some(bss.ssid.as_bytes().to_vec()),
            },
            State::Associated { bss, link_state: LinkState::LinkUp, .. } => Status {
                connected_to: Some(convert_bss_description(bss)),
                connecting_to: None,
            },
        }
    }
}

fn to_deauthenticating_state<T>(current_bss: Box<BssDescription>,
                                next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                                mlme_sink: &MlmeSink) -> State<T>
    where T: Tokens
{
    mlme_sink.send(MlmeRequest::Deauthenticate(
        fidl_mlme::DeauthenticateRequest {
            peer_sta_address: current_bss.bssid.clone(),
            reason_code: fidl_mlme::ReasonCode::StaLeaving,
        }
    ));
    State::Deauthenticating {
        next_cmd: next_bss_to_join
    }
}

fn disconnect_or_join<T>(next_bss_to_join: Option<ConnectCommand<T::ConnectToken>>,
                         mlme_sink: &MlmeSink)
    -> State<T>
    where T: Tokens
{
    match next_bss_to_join {
        Some(next_cmd) => {
            mlme_sink.send(MlmeRequest::Join(
                fidl_mlme::JoinRequest {
                    selected_bss: clone_bss_desc(&next_cmd.bss),
                    join_failure_timeout: DEFAULT_JOIN_FAILURE_TIMEOUT,
                    nav_sync_delay: 0,
                    op_rate_set: vec![]
                }
            ));
            State::Joining { cmd: next_cmd }
        },
        None => State::Idle
    }
}

fn to_associating_state<T>(cmd: ConnectCommand<T::ConnectToken>, mlme_sink: &MlmeSink)
    -> State<T>
    where T: Tokens
{
    let s_rsne = derive_s_rsne(&cmd.bss.bssid[..], cmd.bss.rsn.as_ref());
    let s_rsne_data = s_rsne.as_ref().map(|s_rsne_ref| {
        let mut buf = Vec::with_capacity(s_rsne_ref.len());
        s_rsne_ref.as_bytes(&mut buf);
        buf
    });

    mlme_sink.send(MlmeRequest::Associate(
        fidl_mlme::AssociateRequest {
            peer_sta_address: cmd.bss.bssid.clone(),
            rsn: s_rsne_data,
        }
    ));
    State::Associating { cmd, s_rsne }
}

fn report_connect_finished<T>(token: Option<T::ConnectToken>,
                              user_sink: &UserSink<T>, result: ConnectResult)
    where T: Tokens
{
    if let Some(token) = token {
        user_sink.send(super::UserEvent::ConnectFinished {
            token,
            result
        })
    }
}

fn clone_vht_mcs_nss(m: &fidl_mlme::VhtMcsNss) -> fidl_mlme::VhtMcsNss {
    fidl_mlme::VhtMcsNss {
        rx_max_mcs: m.rx_max_mcs.clone(),
        tx_max_mcs: m.tx_max_mcs.clone(),
        ..*m
    }
}

fn clone_vht_capabilities_info(i: &fidl_mlme::VhtCapabilitiesInfo) -> fidl_mlme::VhtCapabilitiesInfo {
    fidl_mlme::VhtCapabilitiesInfo {
        ..*i
    }
}

fn clone_vht_capabilities(c: &fidl_mlme::VhtCapabilities) -> fidl_mlme::VhtCapabilities {
    fidl_mlme::VhtCapabilities {
        vht_cap_info: clone_vht_capabilities_info(&c.vht_cap_info),
        vht_mcs_nss: clone_vht_mcs_nss(&c.vht_mcs_nss)
    }
}

fn clone_vht_operation(o: &fidl_mlme::VhtOperation) -> fidl_mlme::VhtOperation {
    fidl_mlme::VhtOperation {
        vht_mcs_nss: clone_vht_mcs_nss(&o.vht_mcs_nss),
        ..*o
    }
}

fn clone_bss_desc(d: &fidl_mlme::BssDescription) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: d.bssid.clone(),
        ssid: d.ssid.clone(),
        bss_type: d.bss_type,
        beacon_period: d.beacon_period,
        dtim_period: d.dtim_period,
        timestamp: d.timestamp,
        local_time: d.local_time,

        country: d.country.clone(),
        rsn: d.rsn.clone(),

        cap: fidl_mlme::CapabilityInfo { ..d.cap },

        vht_cap: d.vht_cap.as_ref().map(|v| Box::new(clone_vht_capabilities(v))),
        vht_op:  d.vht_op.as_ref().map(|v| Box::new(clone_vht_operation(v))),

        chan: fidl_mlme::WlanChan {
            primary: d.chan.primary,
            cbw: d.chan.cbw,
            secondary80: d.chan.secondary80,
        },
        rssi_dbm: d.rssi_dbm,
        rcpi_dbmh: d.rcpi_dbmh,
        rsni_dbh: d.rsni_dbh
    }
}

fn derive_s_rsne(bssid: &[u8], rsne: Option<&Vec<u8>>) -> Option<Rsne> {
    // Supported Ciphers and AKMs:
    // Group: CCMP-128, TKIP
    // Pairwise: CCMP-128
    // AKMS: PSK
    let a_rsne = match rsne {
        Some(rsn_data) => match rsne::from_bytes(&rsn_data[..]).to_full_result() {
            Ok(a_rsne) => a_rsne,
            _ => {
                eprintln!("BSS {:?} uses invalid RSNE: {:?}", bssid, &rsn_data[..]);
                return None
            },
        },
        None => return None,
    };

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
        eprintln!("BSS {:?} uses incompatible RSNE: {:?}", bssid, a_rsne);
        return None;
    }

    // If Authenticator's RSNE is supported, construct Supplicant's RSNE.
    let mut s_rsne = Rsne::new();
    s_rsne.group_data_cipher_suite = a_rsne.group_data_cipher_suite;
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

    const BSSID: [u8; 6] = [0u8; 6];

    fn make_cipher(suite_type: u8) -> cipher::Cipher {
        cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
    }

    fn make_akm(suite_type: u8) -> akm::Akm {
        akm::Akm { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
    }

    fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Option<Vec<u8>> {
        let a_rsne = Rsne {
            version: 1,
            group_data_cipher_suite: data.map(|t| make_cipher(t)),
            pairwise_cipher_suites: pairwise.into_iter().map(|t| make_cipher(t)).collect(),
            akm_suites: akms.into_iter().map(|t| make_akm(t)).collect(),
            ..Default::default()
        };
        let mut buf = Vec::with_capacity(a_rsne.len());
        a_rsne.as_bytes(&mut buf);
        Some(buf)
    }

    fn rsne_as_bytes(s_rsne: Rsne) -> Vec<u8> {
        let mut buf = Vec::with_capacity(s_rsne.len());
        s_rsne.as_bytes(&mut buf);
        buf
    }

    #[test]
    fn test_incompatible_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::GCMP_256), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_incompatible_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::BIP_CMAC_256], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_tkip_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::TKIP], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_tkip_group_data_cipher() {
        let a_rsne = make_rsne(Some(cipher::TKIP), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 2, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_ccmp128_group_data_pairwise_cipher_psk() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_mixed_mode() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128, cipher::TKIP], vec![akm::PSK, akm::FT_PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        let expected_rsne_bytes = vec![48, 18, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2];
        assert_eq!(rsne_as_bytes(s_rsne.expect("expected RSNE to be Some")), expected_rsne_bytes);
    }

    #[test]
    fn test_no_group_data_cipher() {
        let a_rsne = make_rsne(None, vec![cipher::CCMP_128], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_no_pairwise_cipher() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![], vec![akm::PSK]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_no_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_incompatible_akm() {
        let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::EAP]);
        let s_rsne = derive_s_rsne(&BSSID[..], a_rsne.as_ref());
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_no_rsne() {
        let s_rsne = derive_s_rsne(&BSSID[..], None);
        assert_eq!(s_rsne, None);
    }

    #[test]
    fn test_iempty_rsne() {
        let s_rsne = derive_s_rsne(&BSSID[..], Some(vec![]).as_ref());
        assert_eq!(s_rsne, None);
    }
}
