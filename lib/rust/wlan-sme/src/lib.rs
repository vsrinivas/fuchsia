// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

pub mod ap;
pub mod client;
pub mod mesh;
mod sink;
pub mod timer;

use fidl_fuchsia_wlan_mlme as fidl_mlme;
use futures::channel::mpsc;

use crate::client::InfoEvent;
use crate::timer::TimedEvent;

pub type Ssid = Vec<u8>;
pub type MacAddr = [u8; 6];

pub struct DeviceInfo {
    pub addr: [u8; 6],
    pub bands: Vec<fidl_mlme::BandCapabilities>,
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
    Start(fidl_mlme::StartRequest),
    Stop(fidl_mlme::StopRequest),
}

pub trait Station {
    type Event;

    fn on_mlme_event(&mut self, event: fidl_mlme::MlmeEvent);
    fn on_timeout(&mut self, timed_event: TimedEvent<Self::Event>);
}

pub type MlmeStream = mpsc::UnboundedReceiver<MlmeRequest>;
pub type InfoStream = mpsc::UnboundedReceiver<InfoEvent>;