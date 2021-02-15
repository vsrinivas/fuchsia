// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{config::AudioGatewayFeatureSupport, error::Error, profile::ProfileEvent};
use {
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::{NetworkInformation, PeerHandlerProxy},
    fuchsia_async::Task,
    fuchsia_bluetooth::{
        profile::{Attribute, ProtocolDescriptor},
        types::PeerId,
    },
    fuchsia_zircon as zx,
    futures::{channel::mpsc, select, StreamExt},
    log::{debug, info, warn},
};

use super::{gain_control::GainControl, PeerRequest};

pub(super) struct PeerTask {
    id: PeerId,
    _local_config: AudioGatewayFeatureSupport,
    _profile_proxy: bredr::ProfileProxy,
    _handler: Option<PeerHandlerProxy>,
    network: NetworkInformation,
    gain_control: GainControl,
}

impl PeerTask {
    pub fn new(
        id: PeerId,
        _profile_proxy: bredr::ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Result<Self, Error> {
        Ok(Self {
            id,
            _local_config: local_config,
            _profile_proxy,
            _handler: None,
            network: NetworkInformation::EMPTY,
            gain_control: GainControl::new()?,
        })
    }

    pub fn spawn(
        id: PeerId,
        profile_proxy: bredr::ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Result<(Task<()>, mpsc::Sender<PeerRequest>), Error> {
        let (sender, receiver) = mpsc::channel(0);
        let peer = Self::new(id, profile_proxy, local_config)?;
        let task = Task::local(peer.run(receiver));
        Ok((task, sender))
    }

    fn on_connection_request(
        &mut self,
        _protocol: Vec<ProtocolDescriptor>,
        _channel: fuchsia_bluetooth::types::Channel,
    ) {
        // TODO (fxbug.dev/64566): handle connection request
    }

    async fn on_search_result(
        &mut self,
        _protocol: Option<Vec<ProtocolDescriptor>>,
        _attributes: Vec<Attribute>,
    ) {
        // TODO (fxbug.dev/64566): handle search result
    }

    /// When a new handler is received, the state is not known. It might be stale because the
    /// system is asynchronous and the PeerHandler connection has two sides. The handler will only
    /// be stored if all the associated fidl calls are successful. Otherwise it is dropped without
    /// being set.
    async fn on_peer_handler(&mut self, handler: PeerHandlerProxy) -> Result<(), Error> {
        info!("Got request to handle peer {} headset using handler", self.id);
        // Getting the network information the first time should always return a complete table.
        // If the call returns an error, do not set up the handler.
        let info = match handler.watch_network_information().await {
            Ok(info) => info,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. }) => {
                return Ok(());
            }
            Err(e) => {
                warn!("Error handling peer request: {}", e);
                return Ok(());
            }
        };

        update_network_information(&mut self.network, info);

        let client_end = self.gain_control.get_client_end()?;
        if let Err(e) = handler.gain_control(client_end) {
            warn!("Error setting gain control for peer {}: {}", self.id, e);
            return Ok(());
        }

        self._handler = Some(handler);

        Ok(())
    }

    async fn peer_request(&mut self, request: PeerRequest) -> Result<(), Error> {
        match request {
            PeerRequest::Profile(ProfileEvent::ConnectionRequest { protocol, channel, id: _ }) => {
                self.on_connection_request(protocol, channel)
            }
            PeerRequest::Profile(ProfileEvent::SearchResult { protocol, attributes, id: _ }) => {
                self.on_search_result(protocol, attributes).await
            }
            PeerRequest::Handle(handler) => self.on_peer_handler(handler).await?,
        }
        Ok(())
    }

    pub async fn run(mut self, mut task_channel: mpsc::Receiver<PeerRequest>) {
        loop {
            select! {
                // New request coming from elsewhere in the component
                request = task_channel.next() => {
                    if let Some(request) = request {
                        if let Err(e) = self.peer_request(request).await {
                            warn!("Error handling peer request: {}", e);
                            break;
                        }
                    } else {
                        debug!("Peer task channel closed");
                        break;
                    }
                }
                // New request on the gain control protocol
                _request = self.gain_control.select_next_some() => {
                    unimplemented!();
                }
            }
        }

        debug!("Stopping task for peer {}", self.id);
    }
}

/// Update the `network` with values provided by `update`. Any fields in
/// `update` that are `None` will not result in an update to `network`.
fn update_network_information(network: &mut NetworkInformation, update: NetworkInformation) {
    if update.service_available.is_some() {
        network.service_available = update.service_available;
    }
    if update.signal_strength.is_some() {
        network.signal_strength = update.signal_strength;
    }
    if update.roaming.is_some() {
        network.roaming = update.roaming;
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequestStream},
        fidl_fuchsia_bluetooth_hfp::{PeerHandlerMarker, PeerHandlerRequest, SignalStrength},
        fuchsia_async as fasync,
        proptest::prelude::*,
    };

    fn arb_signal() -> impl Strategy<Value = Option<SignalStrength>> {
        proptest::option::of(prop_oneof![
            Just(SignalStrength::None),
            Just(SignalStrength::VeryLow),
            Just(SignalStrength::Low),
            Just(SignalStrength::Medium),
            Just(SignalStrength::High),
            Just(SignalStrength::VeryHigh),
        ])
    }

    prop_compose! {
        fn arb_network()(
            service_available in any::<Option<bool>>(),
            signal_strength in arb_signal(),
            roaming in any::<Option<bool>>()
        ) -> NetworkInformation {
            NetworkInformation {
                service_available,
                roaming,
                signal_strength,
                ..NetworkInformation::EMPTY
            }
        }
    }

    proptest! {
        #[test]
        fn updates(a in arb_network(), b in arb_network()) {
            let mut c = a.clone();
            update_network_information(&mut c, b.clone());

            // Check that the `service_available` field is correct.
            if b.service_available.is_some() {
                assert_eq!(c.service_available, b.service_available);
            } else if a.service_available.is_some() {
                assert_eq!(c.service_available, a.service_available);
            } else {
                assert_eq!(c.service_available, None);
            }

            // Check that the `signal_strength` field is correct.
            if b.signal_strength.is_some() {
                assert_eq!(c.signal_strength, b.signal_strength);
            } else if a.signal_strength.is_some() {
                assert_eq!(c.signal_strength, a.signal_strength);
            } else {
                assert_eq!(c.signal_strength, None);
            }

            // Check that the `roaming` field is correct.
            if b.roaming.is_some() {
                assert_eq!(c.roaming, b.roaming);
            } else if a.roaming.is_some() {
                assert_eq!(c.roaming, a.roaming);
            } else {
                assert_eq!(c.roaming, None);
            }
        }
    }

    /// Perform various updates on the peer's internal NetworkInformation.
    #[test]
    fn network_information_updates_successfully() {
        let mut network = NetworkInformation::EMPTY;

        let update_1 =
            NetworkInformation { service_available: Some(true), ..NetworkInformation::EMPTY };
        update_network_information(&mut network, update_1.clone());
        assert_eq!(network, update_1);

        let update_2 = NetworkInformation {
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };
        update_network_information(&mut network, update_2.clone());
        let expected = NetworkInformation {
            service_available: Some(true),
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };
        assert_eq!(network, expected);

        let update_3 = NetworkInformation {
            service_available: Some(false),
            roaming: Some(false),
            ..NetworkInformation::EMPTY
        };
        update_network_information(&mut network, update_3.clone());
        let expected = NetworkInformation {
            service_available: Some(false),
            signal_strength: Some(SignalStrength::Low),
            roaming: Some(false),
            ..NetworkInformation::EMPTY
        };
        assert_eq!(network, expected);
    }

    fn setup_peer_task(
    ) -> (PeerTask, mpsc::Sender<PeerRequest>, mpsc::Receiver<PeerRequest>, ProfileRequestStream)
    {
        let (sender, receiver) = mpsc::channel(0);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        (
            PeerTask::new(PeerId(1), proxy, AudioGatewayFeatureSupport::default())
                .expect("Could not create PeerTask"),
            sender,
            receiver,
            stream,
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn handle_peer_request_stores_peer_handler_proxy() {
        let mut peer = setup_peer_task().0;
        assert!(peer._handler.is_none());
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        fasync::Task::local(async move {
            match stream.next().await {
                Some(Ok(PeerHandlerRequest::WatchNetworkInformation { responder })) => {
                    responder
                        .send(NetworkInformation::EMPTY)
                        .expect("Successfully send network information");
                }
                x => panic!("Expected watch network information request: {:?}", x),
            };

            // Call manager will wait indefinitely for a request that will not come during this
            // test.
            while let Some(Ok(_)) = stream.next().await {}
        })
        .detach();

        peer.peer_request(PeerRequest::Handle(proxy)).await.expect("peer request to succeed");
        assert!(peer._handler.is_some());
    }

    #[fasync::run_until_stalled(test)]
    async fn handle_peer_request_decline_to_handle() {
        let mut peer = setup_peer_task().0;
        assert!(peer._handler.is_none());
        let (proxy, server_end) = fidl::endpoints::create_proxy::<PeerHandlerMarker>().unwrap();

        // close the PeerHandler channel by dropping the server endpoint.
        drop(server_end);

        peer.peer_request(PeerRequest::Handle(proxy)).await.expect("request to succeed");
        assert!(peer._handler.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn task_runs_until_all_event_sources_close() {
        let (peer, sender, receiver, _) = setup_peer_task();
        // Peer will stop running when all event sources are closed.
        // Close all sources
        drop(sender);

        // Test that `run()` completes.
        peer.run(receiver).await;
    }
}
