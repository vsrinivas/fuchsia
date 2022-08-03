// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_bluetooth::types::{Channel, PeerId};
use profile_client::ProfileEvent;
use tracing::{info, trace};

use crate::config::HandsFreeFeatureSupport;

/// Represents a Bluetooth peer that supports the AG role. Manages the Service Level Connection,
/// Audio Connection, and FIDL APIs
pub struct Peer {
    _id: PeerId,
    _config: HandsFreeFeatureSupport,
    _profile_svc: bredr::ProfileProxy,
    channel: Option<Channel>,
}

impl Peer {
    pub fn new(
        id: PeerId,
        config: HandsFreeFeatureSupport,
        profile_svc: bredr::ProfileProxy,
    ) -> Self {
        Self { _id: id, _config: config, _profile_svc: profile_svc, channel: None }
    }

    pub fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        match event {
            ProfileEvent::PeerConnected { id: _, protocol: _, channel } => {
                if self.channel.replace(channel).is_some() {
                    info!("Overwriting existing RFCOMM connection");
                }
            }
            ProfileEvent::SearchResult { id, protocol, attributes } => {
                trace!("Received search results for {}: {:?}, {:?}", id, protocol, attributes);
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Channel;

    #[fuchsia::test]
    fn peer_channel_properly_extracted() {
        let _exec = fasync::TestExecutor::new().unwrap();
        let (channel_1, _channel_2) = Channel::create();
        let (profile_proxy, _profile_server) =
            fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        let event = ProfileEvent::PeerConnected {
            id: PeerId::random(),
            protocol: Vec::new(),
            channel: channel_1,
        };
        let mut peer =
            Peer::new(PeerId::random(), HandsFreeFeatureSupport::default(), profile_proxy.clone());
        peer.profile_event(event).expect("Profile event terminated incorrectly.");
        assert!(peer.channel.is_some());
    }
}
