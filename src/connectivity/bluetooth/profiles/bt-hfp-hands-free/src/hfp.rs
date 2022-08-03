// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_bluetooth::types::PeerId;
use futures::StreamExt;
use profile_client::{ProfileClient, ProfileEvent};
use std::collections::hash_map::{Entry, HashMap};

use crate::config::HandsFreeFeatureSupport;
use crate::peer::Peer;

pub struct Hfp {
    config: HandsFreeFeatureSupport,
    /// Provides Hfp with a means to drive the `fuchsia.bluetooth.bredr` related APIs.
    profile_client: ProfileClient,
    /// The client connection to the `fuchsia.bluetooth.bredr.Profile` protocol.
    profile_svc: bredr::ProfileProxy,
    /// A collection of discovered and/or connected Bluetooth peers that support the AG role.
    peers: HashMap<PeerId, Peer>,
}

impl Hfp {
    pub fn new(
        profile_client: ProfileClient,
        profile_svc: bredr::ProfileProxy,
        config: HandsFreeFeatureSupport,
    ) -> Self {
        Self { profile_client, profile_svc, config, peers: HashMap::new() }
    }

    /// Run the Hfp object to completion. Runs until an unrecoverable error occurs or there is no
    /// more work to perform because all managed resources have been closed.
    pub async fn run(mut self) -> Result<(), Error> {
        while let Some(event) = self.profile_client.next().await {
            self.handle_profile_event(event?)?;
        }
        Err(format_err!("Profile client terminated early."))
    }

    fn handle_profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        let id = event.peer_id();
        let peer = match self.peers.entry(id) {
            Entry::Vacant(entry) => {
                let peer = Peer::new(id, self.config, self.profile_svc.clone());
                entry.insert(peer)
            }
            Entry::Occupied(entry) => entry.into_mut(),
        };
        peer.profile_event(event)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl_fuchsia_bluetooth_bredr::ProfileRequest::Advertise;

    use crate::profile::register;

    #[fuchsia::test]
    async fn run_loop_early_termination_of_connection_stream() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
                .expect("Create new profile connection");
        let profile = register(proxy.clone(), HandsFreeFeatureSupport::default())
            .expect("ProfileClient Success.");
        let hfp = Hfp::new(profile, proxy, HandsFreeFeatureSupport::default());
        let request = stream.next().await.unwrap().expect("FIDL request is OK");
        let (_responder, receiver) = match request {
            Advertise { receiver, responder, .. } => (responder, receiver.into_proxy().unwrap()),
            _ => panic!("Not an advertisement"),
        };

        drop(receiver);
        let result = hfp.run().await;
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test]
    async fn run_loop_early_termination_of_advertisement() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
                .expect("Create new profile connection");
        let profile = register(proxy.clone(), HandsFreeFeatureSupport::default())
            .expect("ProfileClient Success.");
        let hfp = Hfp::new(profile, proxy, HandsFreeFeatureSupport::default());
        let request = stream.next().await.unwrap().expect("FIDL request is OK");
        match request {
            Advertise { responder, .. } => {
                let _ = responder.send(&mut Ok(())).unwrap();
            }
            _ => panic!("Not an advertisement"),
        };
        let result = hfp.run().await;
        assert_matches!(result, Err(_));
    }
}
