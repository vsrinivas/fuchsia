// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, types as ap_types},
        client::types as client_types,
        regulatory_manager::REGION_CODE_LEN,
    },
    anyhow::Error,
    fidl_fuchsia_wlan_sme,
    futures::channel::oneshot,
};

pub use ieee80211::Ssid;

#[derive(Debug)]
pub struct DisconnectRequest {
    pub network_id: ap_types::NetworkIdentifier,
    pub reason: client_types::DisconnectReason,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct ConnectRequest {
    pub request: client_types::ConnectRequest,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct RecordIdleIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub struct HasIdleIfaceRequest {
    pub responder: oneshot::Sender<bool>,
}

#[derive(Debug)]
pub struct AddIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub struct RemoveIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub struct ScanProxyRequest {
    pub responder: oneshot::Sender<Result<fidl_fuchsia_wlan_sme::ClientSmeProxy, Error>>,
}

#[derive(Debug)]
pub struct StopClientConnectionsRequest {
    pub reason: client_types::DisconnectReason,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct StartClientConnectionsRequest {
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct StartApRequest {
    pub config: ap_fsm::ApConfig,
    pub responder: oneshot::Sender<Result<oneshot::Receiver<()>, Error>>,
}

#[derive(Debug)]
pub struct StopApRequest {
    pub ssid: Ssid,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct StopAllApsRequest {
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub struct HasWpa3IfaceRequest {
    pub responder: oneshot::Sender<bool>,
}

#[derive(Debug)]
pub struct SetCountryRequest {
    pub country_code: Option<[u8; REGION_CODE_LEN]>,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub enum IfaceManagerRequest {
    Disconnect(DisconnectRequest),
    Connect(ConnectRequest),
    RecordIdleIface(RecordIdleIfaceRequest),
    HasIdleIface(HasIdleIfaceRequest),
    AddIface(AddIfaceRequest),
    RemoveIface(RemoveIfaceRequest),
    GetScanProxy(ScanProxyRequest),
    StopClientConnections(StopClientConnectionsRequest),
    StartClientConnections(StartClientConnectionsRequest),
    StartAp(StartApRequest),
    StopAp(StopApRequest),
    StopAllAps(StopAllApsRequest),
    HasWpa3Iface(HasWpa3IfaceRequest),
    #[allow(unused)]
    SetCountry(SetCountryRequest),
}

pub(crate) struct SetCountryOperationState {
    pub client_connections_initially_enabled: bool,
    pub initial_ap_configs: Vec<ap_fsm::ApConfig>,
    pub set_country_result: Result<(), Error>,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

pub(crate) enum IfaceManagerOperation {
    ConfigureStateMachine,
    SetCountry(SetCountryOperationState),
}
