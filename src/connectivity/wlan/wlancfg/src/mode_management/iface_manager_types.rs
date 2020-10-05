// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, types as ap_types},
        client::state_machine as client_fsm,
    },
    anyhow::Error,
    fidl_fuchsia_wlan_sme,
    futures::channel::oneshot,
};

#[derive(Debug)]
pub(crate) struct DisconnectRequest {
    pub network_id: ap_types::NetworkIdentifier,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub(crate) struct ConnectRequest {
    pub request: client_fsm::ConnectRequest,
    pub responder: oneshot::Sender<Result<oneshot::Receiver<()>, Error>>,
}

#[derive(Debug)]
pub(crate) struct RecordIdleIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub(crate) struct HasIdleIfaceRequest {
    pub responder: oneshot::Sender<bool>,
}

#[derive(Debug)]
pub(crate) struct AddIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub(crate) struct RemoveIfaceRequest {
    pub iface_id: u16,
    pub responder: oneshot::Sender<()>,
}

#[derive(Debug)]
pub(crate) struct ScanRequest {
    pub scan_request: fidl_fuchsia_wlan_sme::ScanRequest,
    pub responder: oneshot::Sender<Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, Error>>,
}

#[derive(Debug)]
pub(crate) struct StopClientConnectionsRequest {
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub(crate) struct StartClientConnectionsRequest {
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub(crate) struct StartApRequest {
    pub config: ap_fsm::ApConfig,
    pub responder:
        oneshot::Sender<Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, Error>>,
}

#[derive(Debug)]
pub(crate) struct StopApRequest {
    pub ssid: Vec<u8>,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub(crate) struct StopAllApsRequest {
    pub responder: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug)]
pub(crate) enum IfaceManagerRequest {
    Disconnect(DisconnectRequest),
    Connect(ConnectRequest),
    RecordIdleIface(RecordIdleIfaceRequest),
    HasIdleIface(HasIdleIfaceRequest),
    AddIface(AddIfaceRequest),
    RemoveIface(RemoveIfaceRequest),
    Scan(ScanRequest),
    StopClientConnections(StopClientConnectionsRequest),
    StartClientConnections(StartClientConnectionsRequest),
    StartAp(StartApRequest),
    StopAp(StopApRequest),
    StopAllAps(StopAllApsRequest),
}
