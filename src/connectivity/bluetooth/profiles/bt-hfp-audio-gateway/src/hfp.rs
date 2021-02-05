// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_bluetooth::types::PeerId,
    futures::{select, stream::StreamExt},
    std::collections::hash_map::{Entry, HashMap},
};

use crate::{
    call_manager::{CallManager, CallManagerEvent},
    config::AudioGatewayFeatureSupport,
    error::Error,
    peer::Peer,
    profile::{Profile, ProfileEvent},
};

/// Manages operation of the HFP functionality.
pub struct Hfp {
    config: AudioGatewayFeatureSupport,
    /// The `profile` provides Hfp with a means to drive the fuchsia.bluetooth.bredr related APIs.
    profile: Profile,
    /// The `call_manager` provides Hfp with a means to interact with clients of the
    /// fuchsia.bluetooth.hfp.Hfp and fuchsia.bluetooth.hfp.CallManager protocols.
    call_manager: CallManager,
    /// A collection of Bluetooth peers that support the HFP profile.
    peers: HashMap<PeerId, Peer>,
}

impl Hfp {
    /// Create a new `Hfp` with the provided `profile`.
    pub fn new(
        profile: Profile,
        call_manager: CallManager,
        config: AudioGatewayFeatureSupport,
    ) -> Self {
        Self { profile, call_manager, peers: HashMap::new(), config }
    }

    /// Run the Hfp object to completion. Runs until an unrecoverable error occurs or there is no
    /// more work to perform because all managed resource have been closed.
    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                event = self.profile.select_next_some() => {
                    self.handle_profile_event(event?).await?;
                }
                event = self.call_manager.select_next_some() => {
                    self.handle_call_manager_event(event).await?;
                }
            }
        }
    }

    /// Handle a single `ProfileEvent` from `profile`.
    async fn handle_profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        let id = event.peer_id();
        let peer = match self.peers.entry(id) {
            Entry::Vacant(entry) => {
                let mut peer = Peer::new(id, self.profile.proxy(), self.config);
                let server_end = peer.build_handler().await?;
                self.call_manager.peer_added(peer.id(), server_end).await;
                entry.insert(peer)
            }
            Entry::Occupied(entry) => entry.into_mut(),
        };
        peer.profile_event(event).await;
        Ok(())
    }

    /// Handle a single `CallManagerEvent` from `call_manager`.
    async fn handle_call_manager_event(&mut self, event: CallManagerEvent) -> Result<(), Error> {
        match event {
            CallManagerEvent::ManagerRegistered => {
                let mut server_ends = Vec::with_capacity(self.peers.len());
                for (id, peer) in self.peers.iter_mut() {
                    server_ends.push((*id, peer.build_handler().await?));
                }
                for (id, server) in server_ends {
                    self.call_manager.peer_added(id, server).await;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::profile::test_server::setup_profile_and_test_server,
        fidl_fuchsia_bluetooth as bt, fuchsia_async as fasync,
    };

    #[fasync::run_until_stalled(test)]
    async fn new_profile_event_calls_handler() {
        let (profile, mut server) = setup_profile_and_test_server();
        let _server_task = fasync::Task::local(async move {
            server.complete_registration().await;
            server
                .results
                .as_ref()
                .unwrap()
                .service_found(&mut bt::PeerId { value: 1 }, None, &mut vec![].iter_mut())
                .await
                .expect("service found to send");
        });

        let hfp = Hfp::new(profile, CallManager::new(), AudioGatewayFeatureSupport::default());
        let result = hfp.run().await;
        assert!(result.is_err());
    }

    #[fasync::run_until_stalled(test)]
    async fn profile_error_propagates_error_from_hfp_run() {
        let (profile, server) = setup_profile_and_test_server();
        // dropping the server is expected to produce an error from Hfp::run
        drop(server);

        let hfp = Hfp::new(profile, CallManager::new(), AudioGatewayFeatureSupport::default());
        let result = hfp.run().await;
        assert!(result.is_err());
    }
}
