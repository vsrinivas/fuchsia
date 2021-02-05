// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{config::AudioGatewayFeatureSupport, error::Error, profile::ProfileEvent};
use {
    fidl::endpoints::ServerEnd, fidl_fuchsia_bluetooth_bredr::ProfileProxy,
    fidl_fuchsia_bluetooth_hfp::PeerHandlerMarker, fuchsia_bluetooth::types::PeerId,
};

/// Represents a Bluetooth peer that implements the HFP Hands Free role.
/// Peer implements Future which completes when the peer is removed from the system
/// and should be cleaned up.
pub struct Peer {
    id: PeerId,
    _local_config: AudioGatewayFeatureSupport,
    _profile_proxy: ProfileProxy,
}

impl Peer {
    pub fn new(
        id: PeerId,
        profile_proxy: ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Self {
        Self { id, _local_config: local_config, _profile_proxy: profile_proxy }
    }

    pub fn id(&self) -> PeerId {
        self.id
    }

    /// Pass a new profile event into the Peer. The Peer can then react to the event as it sees
    /// fit. This method will return once the peer accepts the event.
    pub async fn profile_event(&mut self, _event: ProfileEvent) {}

    /// Create a FIDL channel that can be used to manage this Peer and return the server end.
    pub async fn build_handler(&mut self) -> Result<ServerEnd<PeerHandlerMarker>, Error> {
        let (_proxy, server_end) = fidl::endpoints::create_proxy()
            .map_err(|e| Error::system("Could not create peer handler fidl endpoints", e))?;
        Ok(server_end)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_bredr::ProfileMarker, fuchsia_async as fasync};

    #[test]
    fn peer_id_returns_expected_id() {
        // Executor must exist in order to create fidl endpoints
        let _exec = fasync::Executor::new().unwrap();

        let id = PeerId(1);
        let (proxy, _) = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap();
        let peer = Peer::new(id, proxy, AudioGatewayFeatureSupport::default());
        assert_eq!(peer.id(), id);
    }
}
