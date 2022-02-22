// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;
pub mod serve;
#[cfg(test)]
pub mod test_utils;

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent};
use futures::channel::mpsc;
use thiserror::Error;
use wlan_common::{sink::UnboundedSink, timer::TimedEvent};

#[derive(Copy, Clone, Debug, Default, Eq, PartialEq, Ord, PartialOrd)]
pub struct Config {
    pub wep_supported: bool,
    pub wpa1_supported: bool,
}

impl Config {
    pub fn with_wep(mut self) -> Self {
        self.wep_supported = true;
        self
    }

    pub fn with_wpa1(mut self) -> Self {
        self.wpa1_supported = true;
        self
    }
}

#[derive(Debug)]
pub enum MlmeRequest {
    Scan(fidl_mlme::ScanRequest),
    Join(fidl_mlme::JoinRequest),
    Authenticate(fidl_mlme::AuthenticateRequest),
    AuthResponse(fidl_mlme::AuthenticateResponse),
    Associate(fidl_mlme::AssociateRequest),
    AssocResponse(fidl_mlme::AssociateResponse),
    Deauthenticate(fidl_mlme::DeauthenticateRequest),
    Eapol(fidl_mlme::EapolRequest),
    SetKeys(fidl_mlme::SetKeysRequest),
    SetCtrlPort(fidl_mlme::SetControlledPortRequest),
    Start(fidl_mlme::StartRequest),
    Stop(fidl_mlme::StopRequest),
    SendMpOpenAction(fidl_mlme::MeshPeeringOpenAction),
    SendMpConfirmAction(fidl_mlme::MeshPeeringConfirmAction),
    MeshPeeringEstablished(fidl_mlme::MeshPeeringParams),
    SaeHandshakeResp(fidl_mlme::SaeHandshakeResponse),
    SaeFrameTx(fidl_mlme::SaeFrame),
    WmmStatusReq,
    FinalizeAssociation(fidl_mlme::NegotiatedCapabilities),
}

pub trait Station {
    type Event;

    fn on_mlme_event(&mut self, event: fidl_mlme::MlmeEvent);
    fn on_timeout(&mut self, timed_event: TimedEvent<Self::Event>);
}

pub type MlmeStream = mpsc::UnboundedReceiver<MlmeRequest>;
pub type MlmeSink = UnboundedSink<MlmeRequest>;

mod responder {
    use futures::channel::oneshot;

    #[derive(Debug)]
    pub struct Responder<T>(oneshot::Sender<T>);

    impl<T> Responder<T> {
        pub fn new() -> (Self, oneshot::Receiver<T>) {
            let (sender, receiver) = oneshot::channel();
            (Responder(sender), receiver)
        }

        pub fn respond(self, result: T) {
            self.0.send(result).unwrap_or_else(|_| ());
        }
    }
}

/// Safely log MlmeEvents without printing private information.
fn mlme_event_name(event: &MlmeEvent) -> &str {
    match event {
        MlmeEvent::OnScanResult { .. } => "OnScanResult",
        MlmeEvent::OnScanEnd { .. } => "OnScanEnd",
        MlmeEvent::JoinConf { .. } => "JoinConf",
        MlmeEvent::AuthenticateConf { .. } => "AuthenticateConf",
        MlmeEvent::AuthenticateInd { .. } => "AuthenticateInd",
        MlmeEvent::DeauthenticateConf { .. } => "DeauthenticateConf",
        MlmeEvent::DeauthenticateInd { .. } => "DeauthenticateInd",
        MlmeEvent::AssociateConf { .. } => "AssociateConf",
        MlmeEvent::AssociateInd { .. } => "AssociateInd",
        MlmeEvent::DisassociateConf { .. } => "DisassociateConf",
        MlmeEvent::DisassociateInd { .. } => "DisassociateInd",
        MlmeEvent::SetKeysConf { .. } => "SetKeysConf",
        MlmeEvent::StartConf { .. } => "StartConf",
        MlmeEvent::StopConf { .. } => "StopConf",
        MlmeEvent::EapolConf { .. } => "EapolConf",
        MlmeEvent::IncomingMpOpenAction { .. } => "IncomingMpOpenAction",
        MlmeEvent::IncomingMpConfirmAction { .. } => "IncomingMpConfirmAction",
        MlmeEvent::SignalReport { .. } => "SignalReport",
        MlmeEvent::EapolInd { .. } => "EapolInd",
        MlmeEvent::StatsQueryResp { .. } => "StatsQueryResp",
        MlmeEvent::RelayCapturedFrame { .. } => "RelayCapturedFrame",
        MlmeEvent::OnChannelSwitched { .. } => "OnChannelSwitched",
        MlmeEvent::OnPmkAvailable { .. } => "OnPmkAvailable",
        MlmeEvent::OnSaeHandshakeInd { .. } => "OnSaeHandshakeInd",
        MlmeEvent::OnSaeFrameRx { .. } => "OnSaeFrameRx",
        MlmeEvent::OnWmmStatusResp { .. } => "OnWmmStatusResp",
    }
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("scan end while not scanning")]
    ScanEndNotScanning,
    #[error("scan end with wrong txn id")]
    ScanEndWrongTxnId,
    #[error("scan result while not scanning")]
    ScanResultNotScanning,
    #[error("scan result with wrong txn id")]
    ScanResultWrongTxnId,
}
