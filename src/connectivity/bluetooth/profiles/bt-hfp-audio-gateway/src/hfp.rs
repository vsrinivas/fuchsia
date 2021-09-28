// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::stream::FutureMap,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::{CallManagerProxy, PeerHandlerMarker},
    fidl_fuchsia_bluetooth_hfp_test as hfp_test,
    fuchsia_bluetooth::profile::find_service_classes,
    fuchsia_bluetooth::types::PeerId,
    futures::{
        channel::mpsc::{self, Receiver, Sender},
        select,
        stream::StreamExt,
    },
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

pub enum Event {
    PeerConnected {
        peer_id: PeerId,
        manager_id: ManagerConnectionId,
        handle: ServerEnd<PeerHandlerMarker>,
    },
}

/// Manages operation of the HFP functionality.
pub struct Hfp {
    config: AudioGatewayFeatureSupport,
    /// The `profile_client` provides Hfp with a means to drive the fuchsia.bluetooth.bredr related APIs.
    profile_client: ProfileClient,
    /// The client connection to the `fuchsia.bluetooth.bredr.Profile` protocol.
    profile_svc: bredr::ProfileProxy,
    /// The `call_manager` provides Hfp with a means to interact with clients of the
    /// fuchsia.bluetooth.hfp.Hfp and fuchsia.bluetooth.hfp.CallManager protocols.
    call_manager: CallManager,
    call_manager_registration: Receiver<CallManagerProxy>,
    /// A collection of Bluetooth peers that support the HFP profile.
    peers: FutureMap<PeerId, Box<dyn Peer>>,
    test_requests: Receiver<hfp_test::HfpTestRequest>,
    connection_behavior: ConnectionBehavior,
    /// A shared audio controller, to start and route audio devices for peers.
    audio: Arc<Mutex<Box<dyn AudioControl>>>,
    internal_events_rx: Receiver<Event>,
    internal_events_tx: Sender<Event>,
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
        let (internal_events_tx, internal_events_rx) = mpsc::channel(1);

        Self {
            profile_client,
            profile_svc,
            call_manager_registration,
            call_manager: CallManager::default(),
            peers: FutureMap::new(),
            config,
            test_requests,
            connection_behavior: ConnectionBehavior::default(),
            audio: Arc::new(Mutex::new(Box::new(audio))),
            internal_events_rx,
            internal_events_tx,
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
                    let _ = removed.map(|id| debug!("Peer removed: {}", id));
                }
                event = self.internal_events_rx.select_next_some() => {
                    self.handle_internal_event(event).await;
                }
                complete => {
                    break;
                }
            }
        }
        Ok(())
    }

    async fn handle_internal_event(&mut self, event: Event) {
        match event {
            Event::PeerConnected { peer_id, manager_id, handle } => {
                let current_id = self.call_manager.connection_id();
                if let Some(proxy) = self.call_manager.proxy() {
                    if manager_id != current_id {
                        // This message is for an old manager connection.
                        // It should be ignored.
                        return;
                    }
                    if let Err(e) = proxy.peer_connected(&mut peer_id.into(), handle).await {
                        if e.is_closed() {
                            info!("CallManager channel closed.");
                        } else {
                            info!("CallManager channel closed with error: {}", e);
                        }
                    }
                }
            }
        }
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
        // Check if the search result is really a HandsFree before adding the peer.
        if let ProfileEvent::SearchResult { attributes, .. } = &event {
            let classes = find_service_classes(attributes);
            if classes
                .iter()
                .find(|an| {
                    an.number == bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway as u16
                })
                .is_some()
            {
                info!(?id, "Search returned AudioGateway, skipping");
                return Ok(());
            }
        }

        let peer = match self.peers.inner().entry(id) {
            Entry::Vacant(entry) => {
                let mut peer = Box::new(PeerImpl::new(
                    id,
                    self.profile_svc.clone(),
                    self.audio.clone(),
                    self.config,
                    self.connection_behavior,
                    self.internal_events_tx.clone(),
                )?);
                if self.call_manager.connected() {
                    // Peer should be able to accept call_manager_connected request immediately
                    // after the Peer was constructed.
                    peer.call_manager_connected(self.call_manager.connection_id()).await?;
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
        if self.call_manager.connected() {
            info!("Call manager already set. Closing new connection");
            return Ok(());
        }

        self.call_manager.new_connection(proxy);

        // Propagate new connection id to peers.
        for (_, peer) in self.peers.inner().iter_mut() {
            let _ = peer.call_manager_connected(self.call_manager.connection_id()).await;
        }

        Ok(())
    }
}

/// Unique identifier for a given connection between the CallManager and the HFP component.
#[derive(Copy, Clone, PartialEq, Default, Debug)]
pub struct ManagerConnectionId(usize);

#[derive(Default)]
pub struct CallManager {
    id: ManagerConnectionId,
    proxy: Option<CallManagerProxy>,
}

impl CallManager {
    /// Assign a new proxy to the CallManager.
    pub fn new_connection(&mut self, proxy: CallManagerProxy) {
        self.id.0 = self.id.0.wrapping_add(1);
        self.proxy = Some(proxy);
    }

    /// Returns true if the proxy is present and connected.
    pub fn connected(&self) -> bool {
        matches!(&self.proxy, Some(proxy) if !proxy.is_closed())
    }

    pub fn proxy(&self) -> Option<&CallManagerProxy> {
        self.proxy.as_ref()
    }

    pub fn connection_id(&self) -> ManagerConnectionId {
        self.id
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
        async_test_helpers::run_while,
        async_utils::PollExt,
        fidl_fuchsia_bluetooth as bt,
        fidl_fuchsia_bluetooth_hfp::{
            CallManagerMarker, CallManagerRequest, CallManagerRequestStream,
        },
        fuchsia_async as fasync,
        futures::{channel::mpsc, SinkExt, TryStreamExt},
    };

    use crate::audio::TestAudioControl;
    use fidl_fuchsia_bluetooth_bredr as bredr;
    use fuchsia_bluetooth::types::Uuid;
    use fuchsia_zircon as zx;

    #[fuchsia::test(allow_stalls = false)]
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
    #[fuchsia::test(allow_stalls = false)]
    async fn new_profile_event_initiates_connections_to_profile_and_call_manager() {
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

        // Setup profile, then connect RFCOMM channel.
        let _server = profile_server_init_and_peer_handling(server, true)
            .await
            .expect("peer setup to complete");

        // Peer Connected notification occurs after channel is connected.
        assert!(
            call_manager_init_and_peer_handling(stream).await.is_ok(),
            "call manager to be notified"
        );
    }

    /// Tests the HFP main run loop from a blackbox perspective by asserting on the FIDL messages
    /// sent and received by the services that Hfp interacts with: A bredr profile server and
    /// a call manager.
    #[fuchsia::test]
    fn peer_connected_only_after_connection_success() {
        let mut exec = fuchsia_async::TestExecutor::new().unwrap();
        let (profile, profile_svc, server) = setup_profile_and_test_server();
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        exec.run_singlethreaded(sender.send(proxy)).expect("Hfp to receive the proxy");

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

        // Setup profile, then connect RFCOMM channel.
        let server =
            exec.run_singlethreaded(profile_server_init_and_peer_handling(server, false)).unwrap();

        // Peer Connected notification occurs after channel is connected.
        let call_manager = call_manager_init_and_peer_handling(stream);
        futures::pin_mut!(call_manager);
        assert!(exec.run_until_stalled(&mut call_manager).is_pending());

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let chan = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            flush_timeout: None,
            ..bredr::Channel::EMPTY
        };

        let mut proto = rfcomm_protocol();
        server
            .receiver
            .as_ref()
            .unwrap()
            .connected(&mut bt::PeerId { value: 1 }, chan, &mut proto.iter_mut())
            .expect("succeed");
        assert!(exec.run_until_stalled(&mut call_manager).is_ready());
    }

    #[fuchsia::test]
    async fn peer_then_first_manager_connected_works() {
        let (profile, profile_svc, server) = setup_profile_and_test_server();
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);

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

        // Setup profile, then connect RFCOMM channel.
        let _server = profile_server_init_and_peer_handling(server, true)
            .await
            .expect("peer setup to complete");

        sender.send(proxy).await.expect("Hfp to receive the proxy");
        // Peer Connected notification occurs after channel is connected.
        assert!(
            call_manager_init_and_peer_handling(stream).await.is_ok(),
            "call manager to be notified"
        );
    }

    // TODO: This test can be enabled once the test synchronizes the call manager channels such that
    // the first call manager is seen as closed by both ends before the second call manager channel
    // is sent into the Hfp task.
    // #[fuchsia::test]
    // async fn manager_disconnect_and_new_connection_works() {
    //     let (profile, profile_svc, server) = setup_profile_and_test_server();
    //     let (proxy, stream) =
    //         fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();

    //     let (mut sender, receiver) = mpsc::channel(1);
    //     sender.send(proxy).await.expect("Hfp to receive the proxy");

    //     let (_, rx) = mpsc::channel(1);

    //     // Run hfp in a background task since we are testing that the profile server observes the
    //     // expected behavior when interacting with hfp.
    //     let hfp = Hfp::new(
    //         profile,
    //         profile_svc,
    //         TestAudioControl::default(),
    //         receiver,
    //         AudioGatewayFeatureSupport::default(),
    //         rx,
    //     );
    //     let _hfp_task = fasync::Task::local(hfp.run());

    //     // Setup profile, then connect RFCOMM channel.
    //     let _server = profile_server_init_and_peer_handling(server, true).await.expect("peer setup to complete");

    //     // Peer Connected notification occurs after channel is connected.
    //     let mut stream = call_manager_init_and_peer_handling(stream).await.expect("call manager to be notified");

    //     // Close call manager stream end
    //     use fidl::endpoints::RequestStream;
    //     stream.control_handle().shutdown();
    //     let _ = stream.next().await;

    //     // Setup a new call manager.
    //     let (proxy, stream) =
    //         fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();
    //     sender.send(proxy).await.expect("Hfp to receive the proxy");

    //     // The new call manager should receive a peer connected notification for the peer that is
    //     // connected.
    //     let _ = call_manager_init_and_peer_handling(stream).await.expect("call manager to be notified");
    // }

    /// Tests the HFP main run loop from a blackbox perspective by asserting on the FIDL messages
    /// sent and received by the services that Hfp interacts with: A bredr profile server and
    /// a call manager.
    #[fuchsia::test]
    fn new_profile_from_audio_gateway_is_ignored() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (profile, profile_svc, mut server) = setup_profile_and_test_server();
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        sender.try_send(proxy).expect("Hfp to receive the proxy");

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

        let hfp_fut = hfp.run();
        futures::pin_mut!(hfp_fut);
        // Complete registration by the peer.
        let ((), hfp_fut) = run_while(&mut exec, hfp_fut, server.complete_registration());

        // Send an AudioGateway service found
        let mut audio_gateway_service_class_attrs = vec![bredr::Attribute {
            id: bredr::ATTR_SERVICE_CLASS_ID_LIST,
            element: bredr::DataElement::Sequence(vec![
                Some(Box::new(bredr::DataElement::Uuid(
                    Uuid::new16(bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway as u16)
                        .into(),
                ))),
                Some(Box::new(bredr::DataElement::Uuid(
                    Uuid::new16(bredr::ServiceClassProfileIdentifier::GenericAudio as u16).into(),
                ))),
            ]),
        }];

        let service_found_fut = server.results.as_ref().unwrap().service_found(
            &mut PeerId(1).into(),
            None,
            &mut audio_gateway_service_class_attrs.iter_mut(),
        );

        let (result, _hfp_fut) = run_while(&mut exec, hfp_fut, service_found_fut);
        result.expect("service_found should complete with success");

        // Call manager should have nothing from this interaction, the HFP should ignore it.
        let res = exec.run_until_stalled(&mut stream.next());
        res.expect_pending("should not send a call request");
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

    fn rfcomm_protocol() -> Vec<bredr::ProtocolDescriptor> {
        let sc = 10;
        vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![bredr::DataElement::Uint8(sc)],
            },
        ]
    }

    /// Respond to all FIDL messages expected during the initialization of the Hfp main run loop and
    /// during the simulation of a new `Peer` search result event.
    ///
    /// Returns Ok(()) when all expected messages have been handled normally.
    async fn profile_server_init_and_peer_handling(
        mut server: LocalProfileTestServer,
        connect_from_search: bool,
    ) -> Result<LocalProfileTestServer, anyhow::Error> {
        server.complete_registration().await;
        let mut proto = rfcomm_protocol();

        // Send search result
        server
            .results
            .as_ref()
            .unwrap()
            .service_found(
                &mut bt::PeerId { value: 1 },
                Some(&mut proto.iter_mut()),
                &mut vec![].iter_mut(),
            )
            .await?;

        match server.stream.next().await {
            Some(Ok(bredr::ProfileRequest::Connect { peer_id, connection: _, responder })) => {
                assert_eq!(peer_id, bt::PeerId { value: 1 });
                if connect_from_search {
                    let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
                    server.connections.push(local);
                    let chan = bredr::Channel {
                        socket: Some(remote),
                        channel_mode: Some(bredr::ChannelMode::Basic),
                        max_tx_sdu_size: Some(1004),
                        flush_timeout: None,
                        ..bredr::Channel::EMPTY
                    };

                    responder.send(&mut Ok(chan)).expect("successfully send connection response");
                } else {
                    responder
                        .send(&mut Err(fidl_fuchsia_bluetooth::ErrorCode::Failed))
                        .expect("successfully send connection failure");
                }
            }
            r => panic!("{:?}", r),
        }
        info!("profile server done");
        Ok(server)
    }

    #[fuchsia::test(allow_stalls = false)]
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

        let _ = hfp.peers.insert(id, Box::new(peer));
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

    #[fuchsia::test(allow_stalls = false)]
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

        let _ = hfp.peers.insert(id, Box::new(peer));
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
