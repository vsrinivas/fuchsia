// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_helpers::maybe_stream::MaybeStream,
    async_utils::stream::FutureMap,
    battery_client::{BatteryClient, BatteryClientError, BatteryInfo},
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::{CallManagerProxy, PeerHandlerMarker},
    fidl_fuchsia_bluetooth_hfp_test as hfp_test,
    fuchsia_bluetooth::profile::find_service_classes,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_derive::{AttachError, Inspect},
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
    inspect::{CallManagerInspect, HfpInspect},
    peer::{indicators::battery_level_to_indicator_value, ConnectionBehavior, Peer, PeerImpl},
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
    /// Provides Hfp with a means to drive the `fuchsia.bluetooth.bredr` related APIs.
    profile_client: ProfileClient,
    /// The client connection to the `fuchsia.bluetooth.bredr.Profile` protocol.
    profile_svc: bredr::ProfileProxy,
    /// Provides Hfp with a means to interact with clients of the `fuchsia.bluetooth.hfp.Hfp` and
    /// `fuchsia.bluetooth.hfp.CallManager` protocols.
    call_manager: CallManager,
    call_manager_registration: Receiver<CallManagerProxy>,
    /// A collection of Bluetooth peers that support the HFP profile.
    peers: FutureMap<PeerId, Box<dyn Peer>>,
    test_requests: Receiver<hfp_test::HfpTestRequest>,
    connection_behavior: ConnectionBehavior,
    /// A shared audio controller, to start and route audio devices for peers.
    audio: Arc<Mutex<Box<dyn AudioControl>>>,
    /// Provides Hfp with battery updates from the `fuchsia.power.BatteryManager` protocol - these
    /// are battery updates about the local (Fuchsia) device.
    battery_client: MaybeStream<BatteryClient>,
    internal_events_rx: Receiver<Event>,
    internal_events_tx: Sender<Event>,
    inspect_node: HfpInspect,
}

impl Inspect for &mut Hfp {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node.iattach(parent, name.as_ref())?;
        self.config.iattach(self.inspect_node.node(), "audio_gateway_feature_support")?;
        self.call_manager.iattach(self.inspect_node.node(), "call_manager")?;
        self.inspect_node.autoconnect.set(self.connection_behavior.autoconnect);
        Ok(())
    }
}

impl Hfp {
    /// Create a new `Hfp` with the provided `profile`, and `audio`
    pub fn new(
        profile_client: ProfileClient,
        profile_svc: bredr::ProfileProxy,
        battery_client: Option<BatteryClient>,
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
            battery_client: battery_client.into(),
            internal_events_rx,
            internal_events_tx,
            inspect_node: Default::default(),
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
                    self.handle_test_request(request).await;
                }
                removed = self.peers.next() => {
                    let _ = removed.map(|id| debug!("Peer removed: {}", id));
                }
                battery_info = self.battery_client.next() => {
                    if let Some(info) = battery_info {
                        self.handle_battery_client_update(info).await;
                    }
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
                self.call_manager.peer_connected(manager_id, peer_id, handle).await;
            }
        }
    }

    async fn handle_battery_client_update(
        &mut self,
        update: Result<BatteryInfo, BatteryClientError>,
    ) {
        let update = match update {
            Err(e) => {
                info!("Error in battery client: {:?}", e);
                return;
            }
            Ok(update) => update,
        };

        if let Some(level_percent) = update.level() {
            self.report_battery_level(battery_level_to_indicator_value(level_percent)).await;
        }
    }

    async fn report_battery_level(&mut self, battery_level: u8) {
        for peer in self.peers.inner().values_mut() {
            peer.report_battery_level(battery_level).await;
        }
    }

    async fn handle_test_request(&mut self, request: hfp_test::HfpTestRequest) {
        info!("Handling test request: {:?}", request);
        use hfp_test::HfpTestRequest::*;
        match request {
            BatteryIndicator { level, .. } => {
                self.report_battery_level(level).await;
            }
            SetConnectionBehavior { behavior, .. } => {
                let behavior = behavior.into();
                for peer in self.peers.inner().values_mut() {
                    peer.set_connection_behavior(behavior).await;
                }
                self.connection_behavior = behavior;
            }
        }
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
                    self.inspect_node.peers.create_child(inspect::unique_name("peer_")),
                )?);
                if let Some(connection_id) = self.call_manager.connection_id() {
                    // Peer should be able to accept call_manager_connected request immediately
                    // after the Peer was constructed.
                    peer.call_manager_connected(connection_id).await?;
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

        let call_manager_id = self.call_manager.new_connection(proxy);

        // Propagate new connection id to peers.
        for (_, peer) in self.peers.inner().iter_mut() {
            let _ = peer.call_manager_connected(call_manager_id).await;
        }

        Ok(())
    }
}

/// Unique identifier for a given connection between the CallManager and the HFP component.
#[derive(Copy, Clone, PartialEq, Default, Debug)]
pub struct ManagerConnectionId(usize);

#[derive(Default, Inspect)]
pub struct CallManager {
    id: ManagerConnectionId,
    proxy: Option<CallManagerProxy>,
    #[inspect(forward)]
    inspect: CallManagerInspect,
}

impl CallManager {
    /// Assign a new proxy to the CallManager - returns the ID that was assigned to the new manager.
    pub fn new_connection(&mut self, proxy: CallManagerProxy) -> ManagerConnectionId {
        self.id.0 = self.id.0.wrapping_add(1);
        self.proxy = Some(proxy);
        self.inspect.new_connection(self.id.0);
        self.id
    }

    /// Returns true if the Call Manager proxy is present and connected.
    pub fn connected(&self) -> bool {
        matches!(&self.proxy, Some(proxy) if !proxy.is_closed())
    }

    /// Returns the ID of the connected Call Manager, or None if disconnected.
    pub fn connection_id(&self) -> Option<ManagerConnectionId> {
        if !self.connected() {
            return None;
        }

        Some(self.id)
    }

    /// Notifies the Call Manager of the connected peer.
    pub async fn peer_connected(
        &mut self,
        manager_id: ManagerConnectionId,
        peer_id: PeerId,
        handle: ServerEnd<PeerHandlerMarker>,
    ) {
        if manager_id != self.id {
            // This message is for an old manager connection - it can be ignored.
            return;
        }

        if let Some(proxy) = &self.proxy {
            if let Err(e) = proxy.peer_connected(&mut peer_id.into(), handle).await {
                if e.is_closed() {
                    info!("CallManager channel closed.");
                    self.inspect.set_disconnected();
                } else {
                    info!("Failed to notify peer_connected for CallManager {:?}: {}", self.id, e);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::audio::TestAudioControl;
    use crate::{
        peer::{fake::PeerFake, ConnectionBehavior, PeerRequest},
        profile::test_server::{setup_profile_and_test_server, LocalProfileTestServer},
    };
    use assert_matches::assert_matches;
    use async_test_helpers::run_while;
    use async_utils::PollExt;
    use bt_rfcomm::{profile::build_rfcomm_protocol, ServerChannel};
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth as bt;
    use fidl_fuchsia_bluetooth_bredr as bredr;
    use fidl_fuchsia_bluetooth_hfp::{
        CallManagerMarker, CallManagerRequest, CallManagerRequestStream,
    };
    use fidl_fuchsia_power as fpower;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Uuid;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_zircon as zx;
    use futures::{pin_mut, SinkExt, TryStreamExt};
    use std::convert::TryFrom;
    use test_battery_manager::TestBatteryManager;

    #[fuchsia::test(allow_stalls = false)]
    async fn profile_error_propagates_error_from_hfp_run() {
        let (profile, profile_svc, server) = setup_profile_and_test_server();
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;

        // dropping the server is expected to produce an error from Hfp::run
        drop(server);

        let (_tx, rx1) = mpsc::channel(1);
        let (_, rx2) = mpsc::channel(1);

        let hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;
        let (proxy, stream) = create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        sender.send(proxy).await.expect("Hfp to receive the proxy");

        let (_, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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
        let setup_fut = TestBatteryManager::make_battery_client_with_test_manager();
        pin_mut!(setup_fut);
        let (battery_client, _test_mgr) = exec.run_singlethreaded(&mut setup_fut);
        let (proxy, stream) = create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        exec.run_singlethreaded(sender.send(proxy)).expect("Hfp to receive the proxy");

        let (_, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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

        // Random RFCOMM protocol.
        let mut proto: Vec<bredr::ProtocolDescriptor> =
            build_rfcomm_protocol(ServerChannel::try_from(10).unwrap())
                .iter()
                .map(Into::into)
                .collect();
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
        let (proxy, stream) = create_proxy_and_stream::<CallManagerMarker>().unwrap();
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;

        let (mut sender, receiver) = mpsc::channel(1);

        let (_, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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
    //     let (battery_client, _test_battery_manager) = TestBatteryManager::make_battery_client_with_test_manager().await;
    //     let (proxy, stream) =
    //         create_proxy_and_stream::<CallManagerMarker>().unwrap();

    //     let (mut sender, receiver) = mpsc::channel(1);
    //     sender.send(proxy).await.expect("Hfp to receive the proxy");

    //     let (_, rx) = mpsc::channel(1);

    //     // Run hfp in a background task since we are testing that the profile server observes the
    //     // expected behavior when interacting with hfp.
    //     let hfp = Hfp::new(
    //         profile,
    //         profile_svc,
    //         Some(battery_client),
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
    //         create_proxy_and_stream::<CallManagerMarker>().unwrap();
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
        let setup_fut = TestBatteryManager::make_battery_client_with_test_manager();
        pin_mut!(setup_fut);
        let (battery_client, _test_mgr) = exec.run_singlethreaded(&mut setup_fut);
        let (proxy, mut stream) = create_proxy_and_stream::<CallManagerMarker>().unwrap();

        let (mut sender, receiver) = mpsc::channel(1);
        sender.try_send(proxy).expect("Hfp to receive the proxy");

        let (_, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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

    /// Respond to all FIDL messages expected during the initialization of the Hfp main run loop and
    /// during the simulation of a new `Peer` search result event.
    ///
    /// Returns Ok(()) when all expected messages have been handled normally.
    async fn profile_server_init_and_peer_handling(
        mut server: LocalProfileTestServer,
        connect_from_search: bool,
    ) -> Result<LocalProfileTestServer, anyhow::Error> {
        server.complete_registration().await;
        // Random RFCOMM protocol.
        let mut proto: Vec<bredr::ProtocolDescriptor> =
            build_rfcomm_protocol(ServerChannel::try_from(10).unwrap())
                .iter()
                .map(Into::into)
                .collect();

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
    async fn battery_level_test_request_is_propagated() {
        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;
        let (_call_mgr_tx, call_mgr_rx) = mpsc::channel(1);
        let (mut test_tx, test_rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the correct battery level is
        // propagated to the `peer_receiver`.
        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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
        let (proxy, mut stream) = create_proxy_and_stream::<hfp_test::HfpTestMarker>().unwrap();
        let fidl_request = {
            proxy.battery_indicator(1).unwrap();
            stream.next().await.unwrap().unwrap()
        };

        // Send the battery level request to `hfp`.
        test_tx.send(fidl_request).await.expect("Hfp received the battery request");

        // Check that the expected request was passed into the peer via `hfp`.
        let peer_request =
            peer_receiver.receiver.next().await.expect("Peer received the BatteryLevel request");
        assert_matches!(peer_request, PeerRequest::BatteryLevel(1));
    }

    #[fuchsia::test]
    fn battery_client_update_is_propagated_to_peer() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let setup_fut = TestBatteryManager::make_battery_client_with_test_manager();
        pin_mut!(setup_fut);
        let (battery_client, test_battery_manager) = exec.run_singlethreaded(&mut setup_fut);

        let (_sender, receiver) = mpsc::channel(1);

        let (_tx, rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the profile server observes the
        // expected behavior when interacting with hfp.
        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
            TestAudioControl::default(),
            receiver,
            AudioGatewayFeatureSupport::default(),
            rx,
        );

        let id = PeerId(123);
        let (mut peer_receiver, peer) = PeerFake::new(id);
        let _ = hfp.peers.insert(id, Box::new(peer));

        let hfp_fut = hfp.run();
        futures::pin_mut!(hfp_fut);

        // Make a battery update via the TestBatteryManager.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Low),
            level_percent: Some(88f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let update_fut = test_battery_manager.send_update(update);
        pin_mut!(update_fut);
        let (res, hfp_fut) = run_while(&mut exec, hfp_fut, update_fut);
        assert_matches!(res, Ok(_));

        // Check that the battery update was passed into the peer via `hfp`.
        let peer_receive_fut = peer_receiver.receiver.next();
        let (peer_request, _hfp_fut) = run_while(&mut exec, hfp_fut, peer_receive_fut);
        assert_matches!(peer_request, Some(PeerRequest::BatteryLevel(_)));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn connection_behavior_request_is_propagated() {
        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;
        let (_call_mgr_tx, call_mgr_rx) = mpsc::channel(1);
        let (mut test_tx, test_rx) = mpsc::channel(1);

        // Run hfp in a background task since we are testing that the correct behavior is
        // propagated to the `peer_receiver`.
        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
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
        let (proxy, mut stream) = create_proxy_and_stream::<hfp_test::HfpTestMarker>().unwrap();
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
        assert_matches!(
            peer_request,
            PeerRequest::Behavior(ConnectionBehavior { autoconnect: false })
        );
    }

    #[fuchsia::test]
    async fn expected_inspect_tree() {
        let inspector = inspect::Inspector::new();
        assert_data_tree!(inspector, root: {});

        let (profile, profile_svc, _server) = setup_profile_and_test_server();
        let (battery_client, _test_battery_manager) =
            TestBatteryManager::make_battery_client_with_test_manager().await;
        let (_tx, rx1) = mpsc::channel(1);
        let (_, rx2) = mpsc::channel(1);

        let mut hfp = Hfp::new(
            profile,
            profile_svc,
            Some(battery_client),
            TestAudioControl::default(),
            rx1,
            AudioGatewayFeatureSupport::default(),
            rx2,
        );

        hfp.iattach(&inspector.root(), "hfp").expect("can attach inspect");
        assert_data_tree!(inspector, root: {
            hfp: {
                audio_gateway_feature_support: {
                    reject_incoming_voice_call: false,
                    three_way_calling: false,
                    in_band_ringtone: false,
                    echo_canceling_and_noise_reduction: false,
                    voice_recognition: false,
                    attach_phone_number_to_voice_tag: false,
                    remote_audio_volume_control: false,
                    respond_and_hold: false,
                    enhanced_call_controls: false,
                    wide_band_speech: false,
                    enhanced_voice_recognition: false,
                    enhanced_voice_recognition_with_text: false,
                },
                call_manager: {
                    manager_connection_id: 0u64,
                    connected: false,
                },
                autoconnect: true,
                peers: {},
            }
        });

        let (call_manager, _call_manager_server) = create_proxy::<CallManagerMarker>().unwrap();
        hfp.handle_new_call_manager(call_manager).await.expect("can set call manager");

        assert_data_tree!(inspector, root: {
            hfp: {
                audio_gateway_feature_support: contains {},
                call_manager: {
                    manager_connection_id: 1u64,
                    connected: true,
                },
                autoconnect: true,
                peers: {},
            }
        });

        // The `connected` status is lazily populated. If the Call Manager goes away, then this
        // status should be updated the next time communication with the Call Manager is attempted.
        drop(_call_manager_server);
        let peer_id = PeerId(123);
        let manager_id = hfp.call_manager.connection_id().expect("just set");
        let (_peer_handler_client, peer_handler_server) =
            create_proxy::<PeerHandlerMarker>().unwrap();
        hfp.handle_internal_event(Event::PeerConnected {
            peer_id,
            manager_id,
            handle: peer_handler_server,
        })
        .await;
        assert_data_tree!(inspector, root: {
            hfp: {
                audio_gateway_feature_support: contains {},
                call_manager: {
                    manager_connection_id: 1u64,
                    connected: false,
                },
                autoconnect: true,
                peers: {},
            }
        });
    }
}
