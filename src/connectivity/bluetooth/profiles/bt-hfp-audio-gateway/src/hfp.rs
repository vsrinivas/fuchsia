// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::stream::FutureMap,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::{CallManagerProxy, PeerHandlerMarker},
    fidl_fuchsia_bluetooth_hfp_test as hfp_test,
    fuchsia_bluetooth::types::PeerId,
    futures::{channel::mpsc::Receiver, select, stream::StreamExt},
    parking_lot::Mutex,
    profile_client::{ProfileClient, ProfileEvent},
    std::{collections::hash_map::Entry, matches, sync::Arc},
    tracing::{debug, info},
};

use crate::{
    audio::AudioControl,
    config::AudioGatewayFeatureSupport,
    error::Error,
    peer::{ConnectionBehavior, Peer, PeerImpl},
};

/// Manages operation of the HFP functionality.
pub struct Hfp {
    config: AudioGatewayFeatureSupport,
    /// The `profile_client` provides Hfp with a means to drive the fuchsia.bluetooth.bredr related APIs.
    profile_client: ProfileClient,
    /// The client connection to the `fuchsia.bluetooth.bredr.Profile` protocol.
    profile_svc: bredr::ProfileProxy,
    /// The `call_manager` provides Hfp with a means to interact with clients of the
    /// fuchsia.bluetooth.hfp.Hfp and fuchsia.bluetooth.hfp.CallManager protocols.
    call_manager: Option<CallManagerProxy>,
    call_manager_registration: Receiver<CallManagerProxy>,
    /// A collection of Bluetooth peers that support the HFP profile.
    peers: FutureMap<PeerId, Box<dyn Peer>>,
    test_requests: Receiver<hfp_test::HfpTestRequest>,
    connection_behavior: ConnectionBehavior,
    /// A shared audio controller, to start and route audio devices for peers.
    audio: Arc<Mutex<Box<dyn AudioControl>>>,
}

impl Hfp {
    /// Create a new `Hfp` with the provided `profile`, and `audio`
    pub fn new(
        profile_client: ProfileClient,
        profile_svc: bredr::ProfileProxy,
        audio: impl AudioControl + 'static,
        call_manager_registration: Receiver<CallManagerProxy>,
        config: AudioGatewayFeatureSupport,
        test_requests: Receiver<hfp_test::HfpTestRequest>,
    ) -> Self {
        Self {
            profile_client,
            profile_svc,
            call_manager_registration,
            call_manager: None,
            peers: FutureMap::new(),
            config,
            test_requests,
            connection_behavior: ConnectionBehavior::default(),
            audio: Arc::new(Mutex::new(Box::new(audio))),
        }
    }

    /// Run the Hfp object to completion. Runs until an unrecoverable error occurs or there is no
    /// more work to perform because all managed resource have been closed.
    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                // If the profile stream ever terminates, the component should shut down.
                event = self.profile_client.next() => {
                    if let Some(event) = event {
                        self.handle_profile_event(event?).await?;
                    } else {
                        break;
                    }
                }
                manager = self.call_manager_registration.select_next_some() => {
                    self.handle_new_call_manager(manager).await?;
                }
                request = self.test_requests.select_next_some() => {
                    self.handle_test_request(request).await?;
                }
                removed = self.peers.next() => {
                    removed.map(|id| debug!("Peer removed: {}", id));
                }
                complete => {
                    break;
                }
            }
        }
        Ok(())
    }

    async fn handle_test_request(
        &mut self,
        request: hfp_test::HfpTestRequest,
    ) -> Result<(), Error> {
        info!("Handling test request: {:?}", request);
        use hfp_test::HfpTestRequest::*;
        match request {
            BatteryIndicator { level, .. } => {
                for peer in self.peers.inner().values_mut() {
                    peer.battery_level(level).await;
                }
            }
            SetConnectionBehavior { behavior, .. } => {
                let behavior = behavior.into();
                for peer in self.peers.inner().values_mut() {
                    peer.set_connection_behavior(behavior).await;
                }
                self.connection_behavior = behavior;
            }
        }
        Ok(())
    }

    /// Handle a single `ProfileEvent` from `profile`.
    async fn handle_profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        let id = event.peer_id();
        let peer = match self.peers.inner().entry(id) {
            Entry::Vacant(entry) => {
                let mut peer = Box::new(PeerImpl::new(
                    id,
                    self.profile_svc.clone(),
                    self.audio.clone(),
                    self.config,
                    self.connection_behavior,
                )?);
                if let Some(proxy) = self.call_manager.clone() {
                    let server_end = peer.build_handler().await?;
                    if Self::send_peer_connected(&proxy, peer.id(), server_end).await.is_err() {
                        self.call_manager = None;
                    }
                }
                entry.insert(Box::pin(peer))
            }
            Entry::Occupied(entry) => entry.into_mut(),
        };
        peer.profile_event(event).await?;
        Ok(())
    }

    /// Handle a single `CallManagerEvent` from `call_manager`.
    async fn handle_new_call_manager(&mut self, proxy: CallManagerProxy) -> Result<(), Error> {
        if matches!(&self.call_manager, Some(manager) if !manager.is_closed()) {
            info!("Call manager already set. Closing new connection");
            return Ok(());
        }

        let mut server_ends = Vec::with_capacity(self.peers.inner().len());
        for (id, peer) in self.peers.inner().iter_mut() {
            server_ends.push((*id, peer.build_handler().await?));
        }

        for (id, server) in server_ends {
            if Self::send_peer_connected(&proxy, id, server).await.is_err() {
                return Ok(());
            }
        }

        self.call_manager = Some(proxy.clone());

        Ok(())
    }

    async fn send_peer_connected(
        proxy: &CallManagerProxy,
        id: PeerId,
        server_end: ServerEnd<PeerHandlerMarker>,
    ) -> Result<(), ()> {
        proxy.peer_connected(&mut id.into(), server_end).await.map_err(|e| {
            if e.is_closed() {
                info!("CallManager channel closed.");
            } else {
                info!("CallManager channel closed with error: {}", e);
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            peer::{fake::PeerFake, ConnectionBehavior, PeerRequest},
            profile::test_server::{setup_profile_and_test_server, LocalProfileTestServer},
        },
        fidl_fuchsia_bluetooth as bt,
        fidl_fuchsia_bluetooth_hfp::{
            CallManagerMarker, CallManagerRequest, CallManagerRequestStream,
        },
        fuchsia_async as fasync,
        futures::{channel::mpsc, SinkExt, TryStreamExt},
    };

    use crate::audio::TestAudioControl;

    #[fasync::run_until_stalled(test)]
    async fn profile_error_propagates_error_from_hfp_run() {
        let (profile, profile_svc, server) = setup_profile_and_test_server();
        // dropping the server is expected to produce an error from Hfp::run
        drop(server);

        let (_tx, rx1) = mpsc::channel(1);
        let (_, rx2) = mpsc::channel(1);

        let hfp = Hfp::new(
            profile,
            profile_svc,
            TestAudioControl::default(),
            rx1,
            AudioGatewayFeatureSupport::default(),
            rx2,
        );
        let result = hfp.run().await;
        assert!(result.is_err());
    }

    /// Tests the HFP main run loop from a blackbox perspective by asserting on the FIDL messages
    /// sent and received by the services that Hfp interacts with: A bredr profile server and
    /// a call manager.
    #[fasync::run_until_stalled(test)]
    async fn new_profile_event_initiates_connections_to_profile_and_call_manager_() {
        let (profile, profile_svc, server) = setup_profile_and_test_server();
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        sender.send(proxy).await.expect("Hfp to receive the proxy");

        let (_, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let hfp = Hfp::new(
            profile,
            profile_svc,
            TestAudioControl::default(),
            receiver,
            AudioGatewayFeatureSupport::default(),
            rx,
        );
        let _hfp_task = fasync::Task::local(hfp.run());

        // Drive both services to expected steady states without any errors.
        let result = futures::future::join(
            profile_server_init_and_peer_handling(server),
            call_manager_init_and_peer_handling(stream),
        )
        .await;
        assert!(result.0.is_ok());
        assert!(result.1.is_ok());
    }

    /// Respond to all FIDL messages expected during the initialization of the Hfp main run loop
    /// and during the simulation of a new `Peer` being added.
    ///
    /// Returns Ok(()) when a peer has made a connection request to the call manager.
    async fn call_manager_init_and_peer_handling(
        mut stream: CallManagerRequestStream,
    ) -> Result<CallManagerRequestStream, anyhow::Error> {
        match stream.try_next().await? {
            Some(CallManagerRequest::PeerConnected { id: _, handle, responder }) => {
                responder.send()?;
                let _ = handle.into_stream()?;
            }
            x => anyhow::bail!("Unexpected request received: {:?}", x),
        };
        Ok(stream)
    }

    /// Respond to all FIDL messages expected during the initialization of the Hfp main run loop and
    /// during the simulation of a new `Peer` search result event.
    ///
    /// Returns Ok(()) when all expected messages have been handled normally.
    async fn profile_server_init_and_peer_handling(
        mut server: LocalProfileTestServer,
    ) -> Result<LocalProfileTestServer, anyhow::Error> {
        server.complete_registration().await;

        // Send search result
        server
            .results
            .as_ref()
            .unwrap()
            .service_found(&mut bt::PeerId { value: 1 }, None, &mut vec![].iter_mut())
            .await?;

        info!("profile server done");
        Ok(server)
    }

    #[fasync::run_until_stalled(test)]
    async fn battery_level_request_is_propagated() {
        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let (_call_mgr_tx, call_mgr_rx) = mpsc::channel(1);
        let (mut test_tx, test_rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the correct battery level is
        // propagated to the `peer_receiver`.
        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            TestAudioControl::default(),
            call_mgr_rx,
            AudioGatewayFeatureSupport::default(),
            test_rx,
        );

        let id = PeerId(0);
        let (mut peer_receiver, peer) = PeerFake::new(id);

        hfp.peers.insert(id, Box::new(peer));
        let _hfp_task = fasync::Task::local(hfp.run());

        // Make a new fidl request by creating a channel and sending the request over the channel.
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<hfp_test::HfpTestMarker>().unwrap();
        let fidl_request = {
            proxy.battery_indicator(1).unwrap();
            stream.next().await.unwrap().unwrap()
        };

        // Send the battery level request to `hfp`.
        test_tx.send(fidl_request).await.expect("Hfp received the battery request");

        // Check that the expected request was passed into the peer via `hfp`.
        let peer_request =
            peer_receiver.receiver.next().await.expect("Peer received the BatteryLevel request");
        matches::assert_matches!(peer_request, PeerRequest::BatteryLevel(1));
    }

    #[fasync::run_until_stalled(test)]
    async fn connection_behavior_request_is_propagated() {
        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let (_call_mgr_tx, call_mgr_rx) = mpsc::channel(1);
        let (mut test_tx, test_rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the correct behavior is
        // propagated to the `peer_receiver`.
        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            TestAudioControl::default(),
            call_mgr_rx,
            AudioGatewayFeatureSupport::default(),
            test_rx,
        );

        let id = PeerId(0);
        let (mut peer_receiver, peer) = PeerFake::new(id);

        hfp.peers.insert(id, Box::new(peer));
        let _hfp_task = fasync::Task::local(hfp.run());

        // Make a new fidl request by creating a channel and sending the request over the channel.
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<hfp_test::HfpTestMarker>().unwrap();
        let fidl_request = {
            let behavior = hfp_test::ConnectionBehavior {
                autoconnect: Some(false),
                ..hfp_test::ConnectionBehavior::EMPTY
            };
            proxy.set_connection_behavior(behavior).unwrap();
            stream.next().await.unwrap().unwrap()
        };

        // Send the behavior request to `hfp`.
        test_tx.send(fidl_request).await.expect("Hfp received the behavior request");

        // Check that the expected request was passed into the peer via `hfp`.
        let peer_request = peer_receiver
            .receiver
            .next()
            .await
            .expect("Peer received the ConnectionBehavior request");
        matches::assert_matches!(
            peer_request,
            PeerRequest::Behavior(ConnectionBehavior { autoconnect: false })
        );
    }
}
