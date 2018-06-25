// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate bytes;
extern crate fidl_fuchsia_wlan_mlme as fidl_mlme;
extern crate fuchsia_zircon as zx;
#[macro_use] extern crate failure;
extern crate futures;
extern crate wlan_rsn;

pub mod client;

use futures::channel::mpsc;
use std::collections::HashSet;

pub struct DeviceCapabilities {
    pub supported_channels: HashSet<u8>
}

pub enum MlmeRequest {
    Scan(fidl_mlme::ScanRequest),
    Join(fidl_mlme::JoinRequest),
    Authenticate(fidl_mlme::AuthenticateRequest),
    Associate(fidl_mlme::AssociateRequest),
    Deauthenticate(fidl_mlme::DeauthenticateRequest),
}

pub trait Station {
    fn on_mlme_event(&mut self, event: fidl_mlme::MlmeEvent);
}

pub type MlmeStream = mpsc::UnboundedReceiver<MlmeRequest>;
