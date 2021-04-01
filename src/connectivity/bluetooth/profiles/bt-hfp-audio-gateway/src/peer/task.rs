// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::client::HangingGetStream,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::{NetworkInformation, PeerHandlerProxy},
    fuchsia_async::Task,
    fuchsia_bluetooth::{
        profile::{Attribute, ProtocolDescriptor},
        types::PeerId,
    },
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::Either,
        select,
        stream::{empty, Empty},
        FutureExt, StreamExt,
    },
    log::{debug, info, warn},
    std::{convert::TryInto, fmt},
};

use super::{
    calls::Calls, gain_control::GainControl, service_level_connection::ServiceLevelConnection,
    PeerRequest,
};
use crate::{
    config::AudioGatewayFeatureSupport,
    error::Error,
    procedure::{InformationRequest, ProcedureMarker},
    profile::ProfileEvent,
    protocol::indicators::{Indicator, Indicators},
};

pub(super) struct PeerTask {
    id: PeerId,
    _local_config: AudioGatewayFeatureSupport,
    profile_proxy: bredr::ProfileProxy,
    handler: Option<PeerHandlerProxy>,
    network: NetworkInformation,
    network_updates: Either<
        HangingGetStream<NetworkInformation>,
        Empty<Result<NetworkInformation, fidl::Error>>,
    >,
    calls: Calls,
    gain_control: GainControl,
    connection: ServiceLevelConnection,
}

impl PeerTask {
    pub fn new(
        id: PeerId,
        profile_proxy: bredr::ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Result<Self, Error> {
        Ok(Self {
            id,
            _local_config: local_config,
            profile_proxy,
            handler: None,
            network: NetworkInformation::EMPTY,
            network_updates: empty().right_stream(),
            calls: Calls::new(None),
            gain_control: GainControl::new()?,
            connection: ServiceLevelConnection::new(),
        })
    }

    pub fn spawn(
        id: PeerId,
        profile_proxy: bredr::ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Result<(Task<()>, mpsc::Sender<PeerRequest>), Error> {
        let (sender, receiver) = mpsc::channel(0);
        let peer = Self::new(id, profile_proxy, local_config)?;
        let task = Task::local(peer.run(receiver).map(|_| ()));
        Ok((task, sender))
    }

    fn on_connection_request(
        &mut self,
        _protocol: Vec<ProtocolDescriptor>,
        channel: fuchsia_bluetooth::types::Channel,
    ) {
        // TODO (fxbug.dev/64566): improve connection handling
        info!("connection request from peer {:?}", self.id);
        self.connection.connect(channel);
    }

    async fn on_search_result(
        &mut self,
        _protocol: Option<Vec<ProtocolDescriptor>>,
        _attributes: Vec<Attribute>,
    ) {
        // TODO (fxbug.dev/64566): improve connection handling
        info!("connecting to peer {:?} from search results", self.id);
        let result = self
            .profile_proxy
            .connect(
                &mut self.id.into(),
                &mut bredr::ConnectParameters::Rfcomm(bredr::RfcommParameters {
                    channel: Some(1),
                    ..bredr::RfcommParameters::EMPTY
                }),
            )
            .await;

        match result {
            Ok(Ok(channel)) => {
                self.connection.connect(channel.try_into().expect("Channel to be valid"))
            }
            r => info!("Error connecting to peer {:?} from search results: {:?}", self.id, r),
        }
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

        self.handle_network_update(info).await;

        let client_end = self.gain_control.get_client_end()?;
        if let Err(e) = handler.gain_control(client_end) {
            warn!("Error setting gain control for peer {}: {}", self.id, e);
            return Ok(());
        }

        self.calls = Calls::new(Some(handler.clone()));

        self.create_network_updates_stream(handler.clone());
        self.handler = Some(handler);

        Ok(())
    }

    /// Set a new HangingGetStream to watch for network information updates.
    fn create_network_updates_stream(&mut self, handler: PeerHandlerProxy) {
        let closure = move || Some(handler.watch_network_information());
        self.network_updates = HangingGetStream::new(Box::new(closure)).left_stream();
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

    /// Processes a `request` for information from an HFP procedure.
    async fn procedure_request(&mut self, request: InformationRequest) {
        let marker = (&request).into();
        match request {
            InformationRequest::GetAgFeatures { response } => {
                let features = (&self._local_config).into();
                // Update the procedure with the retrieved AG update.
                self.connection.receive_ag_request(marker, response(features)).await;
            }
            InformationRequest::GetSubscriberNumberInformation { response } => {
                let result = if let Some(handler) = &mut self.handler {
                    handler.subscriber_number_information().await.ok().unwrap_or_else(Vec::new)
                } else {
                    vec![]
                };
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            InformationRequest::GetAgIndicatorStatus { response } => {
                let status = Indicators {
                    service: self.network.service_available.unwrap_or(false),
                    call: false,
                    callsetup: (),
                    callheld: (),
                    signal: self.network.signal_strength.map(|ss| ss as u8).unwrap_or(0),
                    roam: self.network.roaming.unwrap_or(false),
                    battchg: 5, // TODO: Retrieve battery status from Fuchsia power service.
                };
                // Update the procedure with the retrieved AG update.
                self.connection.receive_ag_request(marker, response(status)).await;
            }
            InformationRequest::GetNetworkOperatorName { response } => {
                let format = self.connection.network_operator_name_format();
                let name = match &self.handler {
                    Some(h) => {
                        let result = h.query_operator().await;
                        if let Err(err) = &result {
                            warn!(
                                "Got error attempting to retrieve operator name from AG: {:}",
                                err
                            );
                        };
                        result.ok().flatten()
                    }
                    None => None,
                };
                let name_option = match (name, format) {
                    (Some(n), Some(_)) => Some(n),
                    _ => None, // The format must be set before getting the network name.
                };
                // Update the procedure with the result of retrieving the AG network name.
                self.connection.receive_ag_request(marker, response(name_option)).await;
            }
            InformationRequest::SendDtmf { code, response } => {
                self.calls.send_dtmf_code(code).await;
                self.connection.receive_ag_request(marker, response()).await;
            }
            InformationRequest::SetNrec { enable, response } => {
                let result = if let Some(handler) = &mut self.handler {
                    if let Ok(Ok(())) = handler.set_nrec_mode(enable).await {
                        Ok(())
                    } else {
                        Err(())
                    }
                } else {
                    Err(())
                };
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            InformationRequest::SpeakerVolumeSynchronization { level, response } => {
                self.gain_control.report_speaker_gain(level);
                self.connection.receive_ag_request(marker, response()).await;
            }
            InformationRequest::MicrophoneVolumeSynchronization { level, response } => {
                self.gain_control.report_microphone_gain(level);
                self.connection.receive_ag_request(marker, response()).await;
            }
            InformationRequest::QueryCurrentCalls { response } => {
                let result = self.calls.current_calls();
                self.connection.receive_ag_request(marker, response(result)).await;
            }
        };
    }

    pub async fn run(mut self, mut task_channel: mpsc::Receiver<PeerRequest>) -> Self {
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
                // A new call state has been received from the call service
                _update = self.calls.select_next_some() => {
                    unimplemented!();
                }
                request = self.connection.select_next_some() => {
                    match request {
                        Ok(r) => self.procedure_request(r).await,
                        Err(e) => warn!("SLC stream error: {:?}", e),
                    }
                }
                update = self.network_updates.next() => {
                    if let Some(update) = stream_item_map_or_log(update, "PeerHandler::WatchNetworkUpdate") {
                        self.handle_network_update(update).await
                    } else {
                        break;
                    }
                }
                complete => break,
            }
        }

        debug!("Stopping task for peer {}", self.id);

        self
    }

    /// Request to send the phone `status` by initiating the Phone Status Indicator
    /// procedure.
    async fn phone_status_update(&mut self, status: Indicator) {
        self.connection.receive_ag_request(ProcedureMarker::PhoneStatus, status.into()).await;
    }

    /// Update the network information with the provided `update` value. Pass updates to the HF if
    /// the service level connection is initialized.
    async fn handle_network_update(&mut self, update: NetworkInformation) {
        if update_table_entry(&mut self.network.service_available, &update.service_available) {
            if self.connection.initialized() {
                let status = Indicator::Service(self.network.service_available.unwrap() as u8);
                self.phone_status_update(status).await;
            }
        }
        if update_table_entry(&mut self.network.signal_strength, &update.signal_strength) {
            if self.connection.initialized() {
                let status = Indicator::Signal(self.network.signal_strength.unwrap() as u8);
                self.phone_status_update(status).await;
            }
        }
        if update_table_entry(&mut self.network.roaming, &update.roaming) {
            if self.connection.initialized() {
                let status = Indicator::Roam(self.network.roaming.unwrap() as u8);
                self.phone_status_update(status).await;
            }
        }
    }
}

/// Table entries are all optional fields. Update `dst` if `src` is present and differs from `dst`.
///
/// Return true if the update occurred.
fn update_table_entry<T: PartialEq + Clone>(dst: &mut Option<T>, src: &Option<T>) -> bool {
    if src.is_some() && src != dst {
        *dst = src.clone();
        true
    } else {
        false
    }
}

/// Take an item from a stream the produces results and return an `Option<T>` when available.
/// Log a message including the `stream_name` when the stream produces an error or is exhausted.
///
/// This is useful when dealing with streams where the only meaningful difference between
/// a terminated stream and an error value is how it should be logged.
fn stream_item_map_or_log<T, E: fmt::Debug>(
    item: Option<Result<T, E>>,
    stream_name: &str,
) -> Option<T> {
    match item {
        Some(Ok(value)) => Some(value),
        Some(Err(e)) => {
            warn!("Error on stream {}: {:?}", stream_name, e);
            None
        }
        None => {
            info!("Stream {} closed", stream_name);
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_utils::PollExt,
        fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequestStream},
        fidl_fuchsia_bluetooth_hfp::{PeerHandlerMarker, PeerHandlerRequest, SignalStrength},
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::Channel,
        futures::{future::ready, pin_mut, SinkExt},
        proptest::prelude::*,
    };

    use crate::{
        peer::service_level_connection::{
            tests::{create_and_initialize_slc, expect_data_received_by_peer},
            SlcState,
        },
        protocol::{features::HfFeatures, indicators::IndicatorsReporting},
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

    fn setup_peer_task(
        connection: Option<ServiceLevelConnection>,
    ) -> (PeerTask, mpsc::Sender<PeerRequest>, mpsc::Receiver<PeerRequest>, ProfileRequestStream)
    {
        let (sender, receiver) = mpsc::channel(1);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        let mut task = PeerTask::new(PeerId(1), proxy, AudioGatewayFeatureSupport::default())
            .expect("Could not create PeerTask");
        if let Some(conn) = connection {
            task.connection = conn;
        }
        (task, sender, receiver, stream)
    }

    proptest! {
        #[test]
        fn updates(a in arb_network(), b in arb_network()) {
            let mut exec = fasync::Executor::new().unwrap();
            let mut task = setup_peer_task(None).0;

            task.network = a.clone();
            exec.run_singlethreaded(task.handle_network_update(b.clone()));

            let c = task.network.clone();

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

    #[fasync::run_until_stalled(test)]
    async fn handle_peer_request_stores_peer_handler_proxy() {
        let mut peer = setup_peer_task(None).0;
        assert!(peer.handler.is_none());
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
        assert!(peer.handler.is_some());
    }

    #[fasync::run_until_stalled(test)]
    async fn handle_peer_request_decline_to_handle() {
        let mut peer = setup_peer_task(None).0;
        assert!(peer.handler.is_none());
        let (proxy, server_end) = fidl::endpoints::create_proxy::<PeerHandlerMarker>().unwrap();

        // close the PeerHandler channel by dropping the server endpoint.
        drop(server_end);

        peer.peer_request(PeerRequest::Handle(proxy)).await.expect("request to succeed");
        assert!(peer.handler.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn task_runs_until_all_event_sources_close() {
        let (peer, sender, receiver, _) = setup_peer_task(None);
        // Peer will stop running when all event sources are closed.
        // Close all sources
        drop(sender);

        // Test that `run()` completes.
        peer.run(receiver).await;
    }

    /// Expects a message to be received by the peer. This expectation does not validate the
    /// contents of the data received.
    #[track_caller]
    fn expect_message_received_by_peer(exec: &mut fasync::Executor, remote: &mut Channel) {
        let mut vec = Vec::new();
        let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
        assert!(exec.run_until_stalled(&mut remote_fut).is_ready());
    }

    #[test]
    fn peer_task_drives_procedure() {
        let mut exec = fasync::Executor::new().unwrap();
        let (mut peer, _sender, receiver, _profile) = setup_peer_task(None);

        // Set up the RFCOMM connection.
        let (local, mut remote) = Channel::create();
        peer.on_connection_request(vec![], local);

        let mut peer_task_fut = Box::pin(peer.run(receiver));
        assert!(exec.run_until_stalled(&mut peer_task_fut).is_pending());

        // Simulate remote peer (HF) sending AT command to start the SLC Init Procedure.
        let features = HfFeatures::empty();
        let command = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command);
        let _ = exec.run_until_stalled(&mut peer_task_fut);
        // We then expect an outgoing message to the peer.
        expect_message_received_by_peer(&mut exec, &mut remote);
    }

    #[test]
    fn network_information_updates_are_relayed_to_peer() {
        // This test produces the following two network updates. Each update is expected to
        // be sent to the remote peer.
        let network_update_1 = NetworkInformation {
            signal_strength: Some(SignalStrength::Low),
            roaming: Some(false),
            ..NetworkInformation::EMPTY
        };
        // Expect to send the Signal and Roam indicators to the peer.
        let expected_data1 = vec![Indicator::Signal(3).into(), Indicator::Roam(0).into()];

        let network_update_2 = NetworkInformation {
            service_available: Some(true),
            roaming: Some(true),
            ..NetworkInformation::EMPTY
        };
        // Expect to send the Service and Roam indicators to the peer.
        let expected_data2 = vec![Indicator::Service(1).into(), Indicator::Roam(1).into()];

        // The value after the updates are applied is expected to be the following
        let expected_network = NetworkInformation {
            service_available: Some(true),
            roaming: Some(true),
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };

        // Set up the executor, peer, and background call manager task
        let mut exec = fasync::Executor::new().unwrap();
        let state = SlcState {
            indicator_events_reporting: IndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };
        let (connection, mut remote) = create_and_initialize_slc(state);
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        fasync::Task::local(async move {
            // A vec to hold all the stream items we don't care about for this test.
            let mut junk_drawer = vec![];

            // Filter out all items that are irrelevant to this particular test, placing them in
            // the junk_drawer.
            let mut stream = stream.filter_map(move |item| {
                let item = match item {
                    Ok(PeerHandlerRequest::WatchNetworkInformation { responder }) => {
                        Some(responder)
                    }
                    x => {
                        junk_drawer.push(x);
                        None
                    }
                };
                ready(item)
            });

            // Send the first network update - should be relayed to the peer.
            let responder = stream.next().await.unwrap();
            responder.send(network_update_1).expect("Successfully send network information");
            expect_data_received_by_peer(&mut remote, expected_data1).await;

            // Send the second network update - should be relayed to the peer.
            let responder = stream.next().await.unwrap();
            responder.send(network_update_2).expect("Successfully send network information");
            expect_data_received_by_peer(&mut remote, expected_data2).await;

            // Call manager should collect all further network requests, without responding.
            stream.collect::<Vec<_>>().await;
        })
        .detach();

        // Pass in the client end connected to the call manager
        let result = exec.run_singlethreaded(sender.send(PeerRequest::Handle(proxy)));
        assert!(result.is_ok());

        // Run the PeerTask until it has no more work to do.
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);
        let result = exec.run_until_stalled(&mut run_fut);
        assert!(result.is_pending());

        // Drop the peer task sender to force the PeerTask's run future to complete
        drop(sender);
        let task = exec.run_until_stalled(&mut run_fut).expect("run_fut to complete");

        // Check that the task's network information contains the expected values
        // based on the updates provided by the call manager task.
        assert_eq!(task.network, expected_network);
    }
}
