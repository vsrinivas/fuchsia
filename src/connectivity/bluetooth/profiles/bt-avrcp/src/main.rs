// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![recursion_limit = "512"]

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use {
    failure::Error,
    fuchsia_async as fasync, fuchsia_syslog,
    futures::{channel::mpsc, try_join},
};

use crate::{peer::PeerManager, profile::ProfileServiceImpl};

mod packets;
mod peer;
mod profile;
mod service;
mod types;

#[cfg(test)]
mod tests;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["avrcp", "avctp"]).expect("Unable to initialize logger");

    // Begin searching for AVRCP target/controller SDP records on newly connected remote peers
    // and register our AVRCP service with the BrEdr profile service.
    let profile_svc = ProfileServiceImpl::connect_and_register_service()
        .await
        .expect("Unable to connect to BrEdr Profile Service");

    // Create a channel that peer manager will receive requests for peer controllers from the FIDL
    // service runner.
    let (client_sender, peer_controller_request_receiver) = mpsc::channel(1);

    let mut peer_manager =
        PeerManager::new(Box::new(profile_svc), peer_controller_request_receiver)
            .expect("Unable to create Peer Manager");

    let service_fut =
        service::run_services(client_sender).expect("Unable to start AVRCP FIDL service");

    let manager_fut = peer_manager.run();

    // Pump both the FIDL service and the peer manager. Neither one should complete unless
    // we are shutting down or there is an unrecoverable error.
    try_join!(service_fut, manager_fut).map(|_| ())
}
