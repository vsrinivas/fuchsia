// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;
use at_commands::{DeserializeBytes, SerDe};
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::{Channel, PeerId};
use futures::StreamExt;
use profile_client::ProfileEvent;
use std::io::Cursor;
use tracing::{trace, warn};

use crate::config::HandsFreeFeatureSupport;

/// Represents a Bluetooth peer that supports the AG role. Manages the Service Level Connection,
/// Audio Connection, and FIDL APIs
pub struct Peer {
    _id: PeerId,
    _config: HandsFreeFeatureSupport,
    _profile_svc: bredr::ProfileProxy,
    /// The processing task for data received from the remote peer over RFCOMM.
    /// This value is None if there is no RFCOMM channel present.
    /// If set, there is no guarantee that the RFCOMM channel is open.
    rfcomm_task: Option<fasync::Task<()>>,
}

impl Peer {
    pub fn new(
        id: PeerId,
        config: HandsFreeFeatureSupport,
        profile_svc: bredr::ProfileProxy,
    ) -> Self {
        Self { _id: id, _config: config, _profile_svc: profile_svc, rfcomm_task: None }
    }

    pub fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        match event {
            ProfileEvent::PeerConnected { id: _, protocol: _, channel } => {
                let processed_channel = process_function(channel);
                let my_task = fasync::Task::spawn(async move {
                    if let Err(e) = processed_channel.await {
                        warn!("Error processing channel: {:?}", e);
                    }
                });

                self.rfcomm_task = Some(my_task);
            }
            ProfileEvent::SearchResult { id, protocol, attributes } => {
                trace!("Received search results for {}: {:?}, {:?}", id, protocol, attributes);
            }
        }
        Ok(())
    }
}

/// Takes in an RFCOMM channel and deserializes incoming data in AT commands.
async fn process_function(mut channel: Channel) -> Result<(), Error> {
    while let Some(bytes_result) = channel.next().await {
        match bytes_result {
            Ok(bytes) => {
                let mut cursor = Cursor::new(&bytes);
                let empty_deserialized_bytes = DeserializeBytes::new();
                let parse_results =
                    at::Response::deserialize(&mut cursor, empty_deserialized_bytes);
                if let Some(error) = parse_results.error {
                    warn!("Could not deserialize correctly {:?}", error);
                }
                // TODO (fxbug.dev/106180): Handle remaining bytes.
                for command in parse_results.values {
                    trace!("Received: {:?}", command);
                }
            }
            Err(e) => return Err(format_err!("Error in RFCOMM connection: {:?}", e)),
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Channel;
    use futures::pin_mut;

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
        assert!(peer.rfcomm_task.is_some());
    }

    #[fuchsia::test]
    fn at_responses_are_accepted() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (local, remote) = Channel::create();

        let processed_channel = process_function(local);
        pin_mut!(processed_channel);
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Sending an at::Response is OK
        let _ = remote.as_ref().write(b"+BRSF:0\r").unwrap();
        // The data should be received by the process function and gracefully handled.
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Remote end disconnects.
        drop(remote);
        let result = exec.run_until_stalled(&mut processed_channel).expect("terminated");
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    fn at_commands_are_handled_gracefully() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (local, remote) = Channel::create();

        let processed_channel = process_function(local);
        pin_mut!(processed_channel);
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // While unexpected, sending an at::Command is OK
        let _ = remote.as_ref().write(b"AT+BRSF=0\r").unwrap();
        // The data should be received by the process function and gracefully handled.
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Remote end disconnects.
        drop(remote);
        let result = exec.run_until_stalled(&mut processed_channel).expect("terminated");
        assert_matches!(result, Ok(_));
    }
}
