// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    async_utils::hanging_get::client::HangingGetStream,
    bt_rfcomm::profile::server_channel_from_protocol,
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
        future::{self, Either, Future},
        select,
        stream::{empty, Empty},
        FutureExt, StreamExt,
    },
    parking_lot::Mutex,
    profile_client::ProfileEvent,
    std::{convert::TryInto, fmt, sync::Arc},
    tracing::{debug, info, warn},
    vigil::{DropWatch, Vigil},
};

use super::{
    calls::{Call, Calls},
    gain_control::GainControl,
    indicators::{AgIndicator, AgIndicators, HfIndicator},
    procedure::ProcedureMarker,
    ringer::Ringer,
    service_level_connection::ServiceLevelConnection,
    slc_request::SlcRequest,
    update::AgUpdate,
    ConnectionBehavior, PeerRequest,
};

use crate::{
    a2dp,
    audio::AudioControl,
    config::AudioGatewayFeatureSupport,
    error::Error,
    features::CodecId,
    sco_connector::{ScoConnection, ScoConnector},
};

pub(super) struct PeerTask {
    id: PeerId,
    _local_config: AudioGatewayFeatureSupport,
    connection_behavior: ConnectionBehavior,
    profile_proxy: bredr::ProfileProxy,
    handler: Option<PeerHandlerProxy>,
    network: NetworkInformation,
    network_updates: Either<
        HangingGetStream<NetworkInformation>,
        Empty<Result<NetworkInformation, fidl::Error>>,
    >,
    battery_level: u8,
    calls: Calls,
    gain_control: GainControl,
    connection: ServiceLevelConnection,
    a2dp_control: a2dp::Control,
    sco_connector: ScoConnector,
    active_sco: Option<Vigil<(ScoConnection, Option<a2dp::PauseToken>)>>,
    ringer: Ringer,
    audio_control: Arc<Mutex<Box<dyn AudioControl>>>,
}

impl PeerTask {
    pub fn new(
        id: PeerId,
        profile_proxy: bredr::ProfileProxy,
        audio_control: Arc<Mutex<Box<dyn AudioControl>>>,
        local_config: AudioGatewayFeatureSupport,
        connection_behavior: ConnectionBehavior,
    ) -> Result<Self, Error> {
        let sco_connector = ScoConnector::build(profile_proxy.clone());
        let a2dp_control = a2dp::Control::connect();
        Ok(Self {
            id,
            _local_config: local_config,
            connection_behavior,
            profile_proxy,
            handler: None,
            network: NetworkInformation::EMPTY,
            network_updates: empty().right_stream(),
            // TODO (fxbug.dev/74667): Retrieve battery status from Fuchsia power service
            battery_level: 5,
            calls: Calls::new(None),
            gain_control: GainControl::new()?,
            connection: ServiceLevelConnection::new(),
            a2dp_control,
            sco_connector,
            active_sco: None,
            ringer: Ringer::default(),
            audio_control,
        })
    }

    pub fn spawn(
        id: PeerId,
        profile_proxy: bredr::ProfileProxy,
        audio_control: Arc<Mutex<Box<dyn AudioControl>>>,
        local_config: AudioGatewayFeatureSupport,
        connection_behavior: ConnectionBehavior,
    ) -> Result<(Task<()>, mpsc::Sender<PeerRequest>), Error> {
        let (sender, receiver) = mpsc::channel(0);
        let peer = Self::new(id, profile_proxy, audio_control, local_config, connection_behavior)?;
        let task = Task::local(peer.run(receiver).map(|_| ()));
        Ok((task, sender))
    }

    /// Always give preference to connection requests received from the peer device.
    fn on_connection_request(
        &mut self,
        _protocol: Vec<ProtocolDescriptor>,
        channel: fuchsia_bluetooth::types::Channel,
    ) {
        if self.connection.connected() {
            info!("Overwriting existing connection to {:?}", self.id);
        } else {
            info!("Connection request from {:?}", self.id);
        }
        self.connection.connect(channel);
    }

    /// Make a connection to the peer only if there is not an existing connection.
    async fn connect(&mut self, mut params: bredr::ConnectParameters) -> Result<(), anyhow::Error> {
        if self.connection.connected() {
            return Ok(());
        }
        info!("Initiating connection to peer");
        let channel = self
            .profile_proxy
            .connect(&mut self.id.into(), &mut params)
            .await?
            .map_err(|e| format_err!("Profile connection request error: {:?}", e))?;
        self.connection.connect(channel.try_into()?);
        Ok(())
    }

    async fn on_search_result(
        &mut self,
        protocol: Option<Vec<ProtocolDescriptor>>,
        _attributes: Vec<Attribute>,
    ) {
        info!("Search results received for {:?}", self.id);

        let server_channel = match protocol.as_ref().map(server_channel_from_protocol) {
            Some(Some(sc)) => sc,
            _ => {
                info!("Search result received for non-RFCOMM protocol: {:?}", protocol);
                return;
            }
        };
        let params = bredr::ConnectParameters::Rfcomm(bredr::RfcommParameters {
            channel: Some(server_channel.into()),
            ..bredr::RfcommParameters::EMPTY
        });

        if self.connection_behavior.autoconnect {
            if let Err(e) = self.connect(params).await {
                info!("Error inititating connecting to peer {:?}: {:?}", self.id, e);
            }
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
            PeerRequest::Profile(ProfileEvent::PeerConnected { protocol, channel, id: _ }) => {
                let protocol = protocol.iter().map(ProtocolDescriptor::from).collect();
                self.on_connection_request(protocol, channel)
            }
            PeerRequest::Profile(ProfileEvent::SearchResult { protocol, attributes, id: _ }) => {
                let protocol = protocol.map(|p| p.iter().map(ProtocolDescriptor::from).collect());
                let attributes = attributes.iter().map(Attribute::from).collect();
                self.on_search_result(protocol, attributes).await
            }
            PeerRequest::Handle(handler) => self.on_peer_handler(handler).await?,
            PeerRequest::BatteryLevel(level) => {
                self.battery_level = level;
                let status = AgIndicator::BatteryLevel(self.battery_level);
                self.phone_status_update(status).await;
            }
            PeerRequest::Behavior(behavior) => {
                self.connection_behavior = behavior;
            }
        }
        Ok(())
    }

    /// Processes a `request` for information from an HFP procedure.
    async fn procedure_request(&mut self, request: SlcRequest) {
        debug!("HF procedure request ({:?}): {:?}", self.id, request);
        let marker = (&request).into();
        match request {
            SlcRequest::GetAgFeatures { response } => {
                let features = (&self._local_config).into();
                // Update the procedure with the retrieved AG update.
                self.connection.receive_ag_request(marker, response(features)).await;
            }
            SlcRequest::GetSubscriberNumberInformation { response } => {
                let result = if let Some(handler) = &mut self.handler {
                    handler.subscriber_number_information().await.ok().unwrap_or_else(Vec::new)
                } else {
                    vec![]
                };
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::GetAgIndicatorStatus { response } => {
                let call_ind = self.calls.indicators();
                let status = AgIndicators {
                    service: self.network.service_available.unwrap_or(false),
                    call: call_ind.call,
                    callsetup: call_ind.callsetup,
                    callheld: call_ind.callheld,
                    signal: self.network.signal_strength.map(|ss| ss as u8).unwrap_or(0),
                    roam: self.network.roaming.unwrap_or(false),
                    battchg: self.battery_level,
                };
                // Update the procedure with the retrieved AG update.
                self.connection.receive_ag_request(marker, response(status)).await;
            }
            SlcRequest::GetNetworkOperatorName { response } => {
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
            SlcRequest::SendDtmf { code, response } => {
                let result = self.calls.send_dtmf_code(code).await;
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::SendHfIndicator { indicator, response } => {
                self.hf_indicator_update(indicator);
                self.connection.receive_ag_request(marker, response()).await;
            }
            SlcRequest::SetNrec { enable, response } => {
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
            SlcRequest::SpeakerVolumeSynchronization { level, response } => {
                self.gain_control.report_speaker_gain(level);
                self.connection.receive_ag_request(marker, response()).await;
            }
            SlcRequest::MicrophoneVolumeSynchronization { level, response } => {
                self.gain_control.report_microphone_gain(level);
                self.connection.receive_ag_request(marker, response()).await;
            }
            SlcRequest::QueryCurrentCalls { response } => {
                let result = self.calls.current_calls();
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::Answer { response } => {
                let result = self.calls.answer().map_err(|e| {
                    warn!("Unexpected Answer from Hands Free: {}", e);
                });
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::HangUp { response } => {
                let result = self.calls.hang_up().map_err(|e| {
                    warn!("Unexpected Hang Up from Hands Free: {}", e);
                });
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::Hold { command, response } => {
                let result = self.calls.hold(command).map_err(|e| {
                    warn!("Unexpected Action {:?} from Hands Free: {}", command, e);
                });
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::InitiateCall { call_action, response } => {
                let result = match &self.handler {
                    Some(h) => match h.request_outgoing_call(&mut call_action.into()).await {
                        Ok(Ok(())) => Ok(()),
                        err => {
                            warn!("Error initiating outgoing call by number: {:?}", err);
                            Err(())
                        }
                    },
                    None => Err(()),
                };
                self.connection.receive_ag_request(marker, response(result)).await;
            }
            SlcRequest::SynchronousConnectionSetup { selected, response } => {
                // TODO(fxbug.dev/72681): Because we must send an OK response to the just before
                // setting up the synchronous connection, we send it here by routing through the
                // procedure.
                if selected.is_some() {
                    self.connection.receive_ag_request(marker, AgUpdate::Ok).await;
                }
                let result = self
                    .setup_audio_connection(selected)
                    .await
                    .map_err(|e| warn!("Error setting up audio connection: {:?}", e));
                self.connection.receive_ag_request(marker, response(result)).await;
            }
        };
    }

    pub async fn run(mut self, mut task_channel: mpsc::Receiver<PeerRequest>) -> Self {
        loop {
            let mut active_sco_closed_fut = self.on_active_sco_closed().fuse();
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
                request = self.gain_control.select_next_some() => {
                    info!("Handling {:?}", request);
                    self.connection.receive_ag_request(ProcedureMarker::VolumeControl, request.into()).await;
                },
                // A new call state has been received from the call service
                update = self.calls.select_next_some() => {
                    info!("Handling {:?}", update);
                    // TODO(fxbug.dev/75538): for in-band ring  setup audio if should_ring is true
                    self.ringer.ring(self.calls.should_ring());
                    if update.callwaiting {
                        if let Some(call) = self.calls.waiting() {
                            self.call_waiting_update(call).await;
                        }
                    }
                    for status in update.to_vec() {
                        self.phone_status_update(status).await;
                    }
                    // If any call is in the active state, then ensure we have an audio connection
                    if self.calls.is_call_active() {
                        self.ensure_audio_connection().await;
                    } else {
                        self.audio_connection_release();
                    }
                }
                request = self.connection.next() => {
                    if let Some(request) = request {
                        match request {
                            Ok(r) => self.procedure_request(r).await,
                            Err(e) => {
                                warn!("SLC stream error: {:?}", e);
                                break;
                            }
                        }
                    } else {
                        debug!("Peer task channel closed");
                        break;
                    }
                }
                _ = active_sco_closed_fut => {
                    self.audio_connection_release();
                }
                update = self.network_updates.next() => {
                    if let Some(update) = stream_item_map_or_log(update, "PeerHandler::WatchNetworkUpdate") {
                        self.handle_network_update(update).await
                    } else {
                        break;
                    }
                }
                _ = self.ringer.select_next_some() => {
                    if let Some(call) = self.calls.ringing() {
                        self.ring_update(call).await;
                    } else {
                        self.ringer.ring(false);
                    }
                }
                complete => break,
            }
        }

        debug!("Stopping task for peer {}", self.id);

        self
    }

    /// Sends an HF Indicator update to the client.
    fn hf_indicator_update(&mut self, indicator: HfIndicator) {
        match indicator {
            ind @ HfIndicator::EnhancedSafety(_) => {
                debug!("Received EnhancedSafety HF Indicator update: {:?}", ind);
            }
            HfIndicator::BatteryLevel(v) => {
                if let Some(handler) = &mut self.handler {
                    if let Err(e) = handler.report_headset_battery_level(v) {
                        warn!("Couldn't report headset battery level: {:?}", e);
                    }
                }
            }
        }
    }

    async fn ensure_audio_connection(&mut self) {
        if self.active_sco.is_some() {
            return;
        }
        self.connection
            .receive_ag_request(ProcedureMarker::CodecConnectionSetup, AgUpdate::CodecSetup(None))
            .await;
    }

    /// Setup the SCO audio connection and start sending audio to the peer.
    async fn setup_audio_connection(&mut self, codec_id: Option<CodecId>) -> Result<(), Error> {
        if self.active_sco.is_some() {
            return Err(Error::OutOfRange);
        }

        info!("Pausing A2DP Audio to setup Audio Connection: {}", self.id);
        let pause_token = match self.a2dp_control.pause(Some(self.id)).await {
            Err(e) => {
                warn!("Couldn't pause A2DP Audio: {:?}", e);
                None
            }
            Ok(token) => Some(token),
        };
        info!("Connecting SCO to {}: {:?}", self.id, codec_id);
        let try_codecs = codec_id.map_or(vec![CodecId::CVSD], |c| vec![c]);
        let connection = self.sco_connector.connect(self.id.clone(), try_codecs).await?;

        let params = connection.params.clone();

        {
            let mut audio = self.audio_control.lock();
            // Start the DAI with the given parameters
            if let Err(e) = audio.start(self.id.clone(), params) {
                // Cancel the SCO connection, we can't send audio.
                // TODO(fxbug.dev/79784): this probably means we should just cancel out of HFP and
                // this peer's connection entirely.
                warn!("Couldn't start Audio DAI ({:?}) - dropping audio connection", e);
                return Err(Error::system(format!("Couldn't start audio DAI"), e));
            }
        }

        let vigil = Vigil::new((connection, pause_token));

        Vigil::watch(&vigil, {
            let control = self.audio_control.clone();
            move |_| match control.lock().stop() {
                Err(e) => warn!("Couldn't stop audio: {:?}", e),
                Ok(()) => info!("Stopped HFP Audio"),
            }
        });

        self.active_sco = Some(vigil);
        Ok(())
    }

    fn on_active_sco_closed(&self) -> impl Future<Output = ()> + 'static {
        match self.active_sco.as_ref() {
            Some(connection) => connection.0.on_closed().left_future(),
            None => future::pending().right_future(),
        }
    }

    /// Close the SCO audio connection and release the pause token on A2DP streaming.
    /// If there is no connection present, this is a no-op.
    fn audio_connection_release(&mut self) {
        drop(self.active_sco.take());
    }

    /// Request to send the phone `status` by initiating the Phone Status Indicator
    /// procedure.
    async fn ring_update(&mut self, call: Call) {
        self.connection.receive_ag_request(ProcedureMarker::Ring, AgUpdate::Ring(call)).await;
    }

    /// Request to send a Call Waiting Notification.
    async fn call_waiting_update(&mut self, call: Call) {
        self.connection
            .receive_ag_request(
                ProcedureMarker::CallWaitingNotifications,
                AgUpdate::CallWaiting(call),
            )
            .await;
    }

    /// Request to send the phone `status` by initiating the Phone Status Indicator
    /// procedure.
    async fn phone_status_update(&mut self, status: AgIndicator) {
        self.connection.receive_ag_request(ProcedureMarker::PhoneStatus, status.into()).await;
    }

    /// Update the network information with the provided `update` value.
    async fn handle_network_update(&mut self, update: NetworkInformation) {
        if update_table_entry(&mut self.network.service_available, &update.service_available) {
            let status = AgIndicator::Service(self.network.service_available.unwrap() as u8);
            self.phone_status_update(status).await;
        }
        if update_table_entry(&mut self.network.signal_strength, &update.signal_strength) {
            let status = AgIndicator::Signal(self.network.signal_strength.unwrap() as u8);
            self.phone_status_update(status).await;
        }
        if update_table_entry(&mut self.network.roaming, &update.roaming) {
            let status = AgIndicator::Roam(self.network.roaming.unwrap() as u8);
            self.phone_status_update(status).await;
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
        at_commands::{self as at, SerDe},
        bt_rfcomm::{profile::build_rfcomm_protocol, ServerChannel},
        core::task::Poll,
        fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequestStream, ScoErrorCode},
        fidl_fuchsia_bluetooth_hfp::{
            CallDirection, CallRequestStream, CallState, NextCall, PeerHandlerMarker,
            PeerHandlerRequest, PeerHandlerRequestStream, PeerHandlerWatchNextCallResponder,
            SignalStrength,
        },
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::Channel,
        futures::{
            future::ready,
            pin_mut,
            stream::{FusedStream, Stream},
            SinkExt,
        },
        matches::assert_matches,
        proptest::prelude::*,
        std::convert::TryFrom,
    };

    use crate::{
        audio::TestAudioControl,
        features::HfFeatures,
        peer::{
            calls::Number,
            indicators::{AgIndicatorsReporting, HfIndicators},
            service_level_connection::{
                tests::{
                    create_and_connect_slc, create_and_initialize_slc,
                    expect_data_received_by_peer, expect_peer_ready, serialize_at_response,
                },
                SlcState,
            },
        },
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
        let audio: Arc<Mutex<Box<dyn AudioControl>>> =
            Arc::new(Mutex::new(Box::new(TestAudioControl::default())));
        let mut task = PeerTask::new(
            PeerId(1),
            proxy,
            audio,
            AudioGatewayFeatureSupport::default(),
            ConnectionBehavior::default(),
        )
        .expect("Could not create PeerTask");
        if let Some(conn) = connection {
            task.connection = conn;
        }
        (task, sender, receiver, stream)
    }

    proptest! {
        #[test]
        fn updates(a in arb_network(), b in arb_network()) {
            let mut exec = fasync::TestExecutor::new().unwrap();
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

    /// Run a background task while waiting for a future that should occur.
    /// This is useful for running a task which you expect to produce side effects that
    /// mean the task is operating correctly. i.e. reacting to a peer action by producing a
    /// response on a client's hanging get.
    /// `background_fut` is expected not to finish ans is returned to the caller, along with
    /// the result of `result_fut`.  If `background_fut` finishes, this function will panic.
    fn run_while<BackgroundFut, ResultFut, Out>(
        exec: &mut fasync::TestExecutor,
        background_fut: BackgroundFut,
        result_fut: ResultFut,
    ) -> (Out, BackgroundFut)
    where
        BackgroundFut: Future + Unpin,
        ResultFut: Future<Output = Out>,
    {
        pin_mut!(result_fut);
        match exec.run_singlethreaded(&mut futures::future::select(background_fut, result_fut)) {
            Either::Right(r) => r,
            Either::Left(_) => panic!("Background future finished"),
        }
    }

    #[fuchsia::test]
    fn handle_peer_request_stores_peer_handler_proxy() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let mut peer = setup_peer_task(None).0;
        assert!(peer.handler.is_none());
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        {
            let request_fut = peer.peer_request(PeerRequest::Handle(proxy));
            pin_mut!(request_fut);

            let (result, request_fut) = run_while(&mut exec, request_fut, stream.next());
            match result {
                Some(Ok(PeerHandlerRequest::WatchNetworkInformation { responder })) => {
                    responder
                        .send(NetworkInformation::EMPTY)
                        .expect("Successfully send network information");
                }
                x => panic!("Expected watch network information request: {:?}", x),
            };

            // Request future should finish.
            let request_result = exec.run_singlethreaded(request_fut);
            assert!(request_result.is_ok());
        }

        assert!(peer.handler.is_some());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn handle_peer_request_decline_to_handle() {
        let mut peer = setup_peer_task(None).0;
        assert!(peer.handler.is_none());
        let (proxy, server_end) = fidl::endpoints::create_proxy::<PeerHandlerMarker>().unwrap();

        // close the PeerHandler channel by dropping the server endpoint.
        drop(server_end);

        peer.peer_request(PeerRequest::Handle(proxy)).await.expect("request to succeed");
        assert!(peer.handler.is_none());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn task_runs_until_all_event_sources_close() {
        let (peer, sender, receiver, _) = setup_peer_task(None);
        // Peer will stop running when all event sources are closed.
        // Close all sources
        drop(sender);

        // Test that `run()` completes.
        let _ = peer.run(receiver).await;
    }

    /// Expects a message to be received by the peer. This expectation does not validate the
    /// contents of the data received.
    #[track_caller]
    fn expect_message_received_by_peer(exec: &mut fasync::TestExecutor, remote: &mut Channel) {
        let mut vec = Vec::new();
        let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
        assert!(exec.run_until_stalled(&mut remote_fut).is_ready());
    }

    #[fuchsia::test]
    fn peer_task_drives_procedure() {
        let mut exec = fasync::TestExecutor::new().unwrap();
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

    #[fuchsia::test]
    fn network_information_updates_are_relayed_to_peer() {
        // This test produces the following two network updates. Each update is expected to
        // be sent to the remote peer.
        let network_update_1 = NetworkInformation {
            signal_strength: Some(SignalStrength::Low),
            roaming: Some(false),
            ..NetworkInformation::EMPTY
        };
        // Expect to send the Signal and Roam indicators to the peer.
        let expected_data1 = vec![AgIndicator::Signal(3).into(), AgIndicator::Roam(0).into()];

        let network_update_2 = NetworkInformation {
            service_available: Some(true),
            roaming: Some(true),
            ..NetworkInformation::EMPTY
        };
        // Expect to send the Service and Roam indicators to the peer.
        let expected_data2 = vec![AgIndicator::Service(1).into(), AgIndicator::Roam(1).into()];

        // The value after the updates are applied is expected to be the following
        let expected_network = NetworkInformation {
            service_available: Some(true),
            roaming: Some(true),
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };

        // Set up the executor, peer, and background call manager task
        let mut exec = fasync::TestExecutor::new().unwrap();
        let state = SlcState {
            ag_indicator_events_reporting: AgIndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };
        let (connection, mut remote) = create_and_initialize_slc(state);
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        // A vec to hold all the stream items we don't care about for this test.
        let mut junk_drawer = vec![];

        // Filter out all items that are irrelevant to this particular test, placing them in
        // the junk_drawer.
        let mut stream = stream.filter_map(move |item| {
            let item = match item {
                Ok(PeerHandlerRequest::WatchNetworkInformation { responder }) => Some(responder),
                x => {
                    junk_drawer.push(x);
                    None
                }
            };
            ready(item)
        });

        // Pass in the client end connected to the call manager
        let result = exec.run_singlethreaded(sender.send(PeerRequest::Handle(proxy)));
        assert!(result.is_ok());

        // The stream should produce the network update while the peer runs in the background.
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // Send the first network update - should be relayed to the peer.
        let (responder, run_fut) = run_while(&mut exec, run_fut, stream.next());
        responder.unwrap().send(network_update_1).expect("Successfully send network information");
        let ((), run_fut) = run_while(
            &mut exec,
            run_fut,
            expect_data_received_by_peer(&mut remote, expected_data1),
        );

        // Send the second network update - should be relayed to the peer.
        let (responder, run_fut) = run_while(&mut exec, run_fut, stream.next());
        responder.unwrap().send(network_update_2).expect("Successfully send network information");
        let ((), mut run_fut) = run_while(
            &mut exec,
            run_fut,
            expect_data_received_by_peer(&mut remote, expected_data2),
        );

        // Drop the peer task sender to force the PeerTask's run future to complete
        drop(sender);
        let task = exec.run_singlethreaded(&mut run_fut);

        // Check that the task's network information contains the expected values
        // based on the updates provided by the call manager task.
        assert_eq!(task.network, expected_network);
    }

    #[fuchsia::test]
    fn terminated_slc_ends_peer_task() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (connection, remote) = create_and_initialize_slc(SlcState::default());
        let (peer, _sender, receiver, _profile) = setup_peer_task(Some(connection));

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // The peer task is pending with no futher work to do at this time.
        let result = exec.run_until_stalled(&mut run_fut);
        assert!(result.is_pending());

        // Closing the SLC connection will result in the completion of the peer task.
        drop(remote);

        let result = exec.run_until_stalled(&mut run_fut);
        let peer = result.expect("run to complete");
        assert!(peer.connection.is_terminated());
    }

    #[fuchsia::test]
    fn error_in_slc_ends_peer_task() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (connection, remote) = create_and_initialize_slc(SlcState::default());
        let (peer, _sender, receiver, _profile) = setup_peer_task(Some(connection));

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // Produces an error when polling the ServiceLevelConnection stream.
        assert!(remote.as_ref().half_close().is_ok());

        // Error on the SLC connection will result in the completion of the peer task.
        let result = exec.run_until_stalled(&mut run_fut);
        assert!(result.is_ready());
    }

    /// Transform `stream` into a Stream of WatchNextCall responders.
    #[track_caller]
    async fn wait_for_call_stream(
        stream: PeerHandlerRequestStream,
    ) -> impl Stream<Item = PeerHandlerWatchNextCallResponder> {
        filtered_stream(stream, |item| match item {
            PeerHandlerRequest::WatchNextCall { responder } => Ok(responder),
            x => Err(x),
        })
        .await
    }

    /// Transform `stream` into a Stream of `T`.
    ///
    /// `f` is a function that takes a PeerHandlerRequest and either returns an Ok if the request
    /// is relevant to the test or Err if the request irrelevant.
    ///
    /// This test helper function can be used in the common case where the test interacts
    /// with a particular kind of PeerHandlerRequest.
    ///
    /// Initial setup of the handler is done, then a filtered stream is produced which
    /// outputs items based on the result of `f`. Ok return values from `f` are returned from the
    /// `filtered_stream`. Err return values from `f` are not returned from the `filtered_stream`.
    /// Instead they are stored within `filtered_stream` until `filtered_stream` is dropped
    /// so that they do not cause the underlying fidl channel to be closed.
    #[track_caller]
    async fn filtered_stream<T>(
        mut stream: PeerHandlerRequestStream,
        f: impl Fn(PeerHandlerRequest) -> Result<T, PeerHandlerRequest>,
    ) -> impl Stream<Item = T> {
        // Send the network information immediately so the peer can make progress.
        match stream.next().await {
            Some(Ok(PeerHandlerRequest::WatchNetworkInformation { responder })) => {
                responder
                    .send(NetworkInformation::EMPTY)
                    .expect("Successfully send network information");
            }
            x => panic!("Expected watch network information request: {:?}", x),
        };

        // A vec to hold all the stream items we don't care about for this test.
        let mut junk_drawer = vec![];

        // Filter out all items, placing them in the junk_drawer.
        stream.filter_map(move |item| {
            let item = match item {
                Ok(item) => match f(item) {
                    Ok(t) => Some(t),
                    Err(x) => {
                        junk_drawer.push(x);
                        None
                    }
                },
                _ => None,
            };
            ready(item)
        })
    }

    #[fuchsia::test]
    fn call_updates_update_ringer_state() {
        // Set up the executor, peer, and background call manager task
        let mut exec = fasync::TestExecutor::new().unwrap();

        // Setup the peer task with the specified SlcState to enable indicator events.
        let state = SlcState {
            ag_indicator_events_reporting: AgIndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };
        let (connection, mut remote) = create_and_initialize_slc(state);
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        // Pass in the client end connected to the call manager
        let result = exec.run_singlethreaded(sender.send(PeerRequest::Handle(proxy)));
        assert!(result.is_ok());

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);
        let (mut stream, run_fut) = run_while(&mut exec, run_fut, wait_for_call_stream(stream));

        // Send the incoming call
        let (responder, run_fut) = run_while(&mut exec, run_fut, stream.next());
        let (client_end, _call_stream) = fidl::endpoints::create_request_stream().unwrap();
        let next_call = NextCall {
            call: Some(client_end),
            remote: Some("1234567".to_string()),
            state: Some(CallState::IncomingRinging),
            direction: Some(CallDirection::MobileTerminated),
            ..NextCall::EMPTY
        };
        responder.unwrap().send(next_call).expect("Successfully send call information");

        let expected_data = vec![AgIndicator::CallSetup(1).into()];
        let ((), mut run_fut) =
            run_while(&mut exec, run_fut, expect_data_received_by_peer(&mut remote, expected_data));

        // Drop the peer task sender to force the PeerTask's run future to complete
        drop(sender);
        let task = exec.run_until_stalled(&mut run_fut).expect("run_fut to complete");

        // Check that the task's ringer has an active call with the expected call index.
        assert!(task.ringer.ringing());
    }

    #[fuchsia::test]
    fn incoming_hf_indicator_battery_level_is_propagated_to_peer_handler_stream() {
        // Set up the executor, peer, and background call manager task
        let mut exec = fasync::TestExecutor::new().unwrap();

        // Setup the peer task with the specified SlcState to enable the battery level HF indicator.
        let mut hf_indicators = HfIndicators::default();
        hf_indicators.enable_indicators(vec![at::BluetoothHFIndicator::BatteryLevel]);
        let state = SlcState { hf_indicators, ..SlcState::default() };
        let (connection, mut remote) = create_and_initialize_slc(state);
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        // The battery level that will be reported by the peer.
        let expected_level = 79;

        // Pass in the client end connected to the call manager
        let result = exec.run_singlethreaded(sender.send(PeerRequest::Handle(proxy)));
        assert!(result.is_ok());

        // Run the PeerTask.
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        let headset_level_stream_fut = filtered_stream(stream, |item| match item {
            PeerHandlerRequest::ReportHeadsetBatteryLevel { level, .. } => Ok(level),
            x => Err(x),
        });

        let (mut stream, run_fut) = run_while(&mut exec, run_fut, headset_level_stream_fut);

        // Peer sends us a battery level HF indicator update.
        let battery_level_cmd = at::Command::Biev {
            anum: at::BluetoothHFIndicator::BatteryLevel,
            value: expected_level as i64,
        };
        let mut buf = Vec::new();
        at::Command::serialize(&mut buf, &vec![battery_level_cmd]).expect("serialization is ok");
        let _ = remote.as_ref().write(&buf[..]).expect("channel write is ok");

        // Run the main future - the task should receive the HF indicator and report it.
        let (battery_level, _run_fut) = run_while(&mut exec, run_fut, stream.next());
        assert_eq!(battery_level, Some(expected_level));

        // Since we (the AG) received a valid HF indicator, we expect to send an OK back to the peer.
        expect_peer_ready(&mut exec, &mut remote, Some(serialize_at_response(at::Response::Ok)));
    }

    #[fuchsia::test]
    fn call_updates_produce_call_waiting() {
        // Set up the executor, peer, and background call manager task
        let mut exec = fasync::TestExecutor::new().unwrap();

        let raw_number = "1234567";
        let number = Number::from(raw_number);
        let expected_ccwa =
            vec![at::success(at::Success::Ccwa { ty: number.type_(), number: number.into() })];
        let expected_ciev = vec![AgIndicator::CallSetup(1).into()];

        // Setup the peer task with the specified SlcState to enable indicator events.
        let state = SlcState {
            call_waiting_notifications: true,
            ag_indicator_events_reporting: AgIndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };
        let (connection, mut remote) = create_and_initialize_slc(state);
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // Pass in the client end connected to the call manager
        let result = exec.run_singlethreaded(sender.send(PeerRequest::Handle(proxy)));
        assert!(result.is_ok());

        let (mut stream, run_fut) = run_while(&mut exec, run_fut, wait_for_call_stream(stream));

        // Send the incoming waiting call.
        let (responder, run_fut) = run_while(&mut exec, run_fut, stream.next());
        let (client_end, _call_stream) = fidl::endpoints::create_request_stream().unwrap();
        let next_call = NextCall {
            call: Some(client_end),
            remote: Some(raw_number.to_string()),
            state: Some(CallState::IncomingWaiting),
            direction: Some(CallDirection::MobileTerminated),
            ..NextCall::EMPTY
        };
        responder.unwrap().send(next_call).expect("Successfully send call information");

        let ((), run_fut) =
            run_while(&mut exec, run_fut, expect_data_received_by_peer(&mut remote, expected_ccwa));
        let ((), mut run_fut) =
            run_while(&mut exec, run_fut, expect_data_received_by_peer(&mut remote, expected_ciev));

        // Drop the peer task sender to force the PeerTask's run future to complete
        drop(sender);
        let _ = exec.run_until_stalled(&mut run_fut).expect("run_fut to complete");
    }

    #[fuchsia::test]
    fn connection_behavior_request_updates_state() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (peer, mut sender, receiver, mut profile) = setup_peer_task(None);

        let _peer_task = fasync::Task::local(peer.run(receiver));

        // First check that a connection is made when search results are received.
        // Send a valid search result.
        let random_channel_number = ServerChannel::try_from(4).expect("valid server channel");
        let protocol =
            Some(build_rfcomm_protocol(random_channel_number).iter().map(Into::into).collect());
        let event = ProfileEvent::SearchResult {
            id: PeerId(1),
            protocol: protocol.clone(),
            attributes: vec![],
        };
        exec.run_singlethreaded(sender.send(PeerRequest::Profile(event))).expect("Send to succeed");

        // Get a connection request on the `profile` stream.
        let result = exec.run_until_stalled(&mut profile.next());
        let _channel = match result {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Connect {
                connection:
                    bredr::ConnectParameters::Rfcomm(bredr::RfcommParameters {
                        channel: Some(sc), ..
                    }),
                responder,
                ..
            }))) => {
                assert_eq!(sc, u8::from(random_channel_number));
                let (local, remote) = Channel::create();
                let local = local.try_into().unwrap();
                responder.send(&mut Ok(local)).unwrap();

                remote
            }
            x => panic!("unexpected result: {:?}", x),
        };

        // Disable auto connect behavior
        exec.run_singlethreaded(
            sender.send(PeerRequest::Behavior(ConnectionBehavior { autoconnect: false })),
        )
        .expect("Send to succeed");

        // Connection is not made after autoconnect is disabled
        // Send search results.
        let event = ProfileEvent::SearchResult { id: PeerId(1), protocol, attributes: vec![] };
        exec.run_singlethreaded(sender.send(PeerRequest::Profile(event))).expect("Send to succeed");

        // No request is received on stream.
        let result = exec.run_until_stalled(&mut profile.next());
        assert!(result.is_pending());
    }

    #[fuchsia::test]
    fn non_rfcomm_search_result_is_ignored() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (peer, mut sender, receiver, mut profile) = setup_peer_task(None);

        let _peer_task = fasync::Task::local(peer.run(receiver));

        // No connection should be made for a non RFCOMM search result.
        // Send a valid search result with some random L2CAP protocol.
        let protocol = vec![bredr::ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::L2Cap,
            params: vec![bredr::DataElement::Uint16(25)],
        }];
        let event = ProfileEvent::SearchResult {
            id: PeerId(1),
            protocol: Some(protocol),
            attributes: vec![],
        };
        exec.run_singlethreaded(sender.send(PeerRequest::Profile(event))).expect("Send to succeed");

        // No connection request on the `profile` stream.
        assert!(exec.run_until_stalled(&mut profile.next()).is_pending());
    }

    #[fuchsia::test]
    fn connect_request_triggers_connection() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let connection = ServiceLevelConnection::new();
        let (local, mut remote) = Channel::create();
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        assert!(!peer.connection.connected());

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        let event_fut = sender.send(PeerRequest::Profile(ProfileEvent::PeerConnected {
            id: PeerId(0),
            protocol: vec![],
            channel: local,
        }));
        exec.run_singlethreaded(event_fut).unwrap();

        // The peer task is pending with no further work to do at this time.
        let result = exec.run_until_stalled(&mut run_fut);
        assert!(result.is_pending());

        // Closing the SLC connection will result in the completion of the peer task.
        drop(sender);

        let result = exec.run_until_stalled(&mut run_fut);
        let peer = result.expect("run to complete");
        assert!(peer.connection.connected());
        assert!(exec.run_until_stalled(&mut remote.next()).is_pending());
    }

    #[fuchsia::test]
    fn connect_request_replaces_connection() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        // SLC is connected at the start of the test.
        let (connection, mut old_remote) = create_and_connect_slc();
        let (peer, mut sender, receiver, _profile) = setup_peer_task(Some(connection));

        assert!(peer.connection.connected());

        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // create a new connection for the SLC
        let (local, mut new_remote) = Channel::create();
        let event_fut = sender.send(PeerRequest::Profile(ProfileEvent::PeerConnected {
            id: PeerId(0),
            protocol: vec![],
            channel: local,
        }));
        exec.run_singlethreaded(event_fut).unwrap();

        // The peer task is pending with no further work to do at this time.
        let result = exec.run_until_stalled(&mut run_fut);
        assert!(result.is_pending());

        // Closing the SLC connection will result in the completion of the peer task.
        drop(sender);

        let result = exec.run_until_stalled(&mut run_fut);
        let peer = result.expect("run to complete");
        assert!(peer.connection.connected());
        let result = exec.run_until_stalled(&mut old_remote.next());
        // old_remote is closed
        assert_matches!(result, Poll::Ready(None));
        // new_remote is open
        assert!(exec.run_until_stalled(&mut new_remote.next()).is_pending());
    }

    /// Run a PeerTask until it has no more work left to do, then return it.
    /// This function generates its own PeerTask channel for convenience. This means that
    /// the channel cannot be used to send messages to a running PeerTask.
    #[track_caller]
    fn run_peer_until_stalled(exec: &mut fasync::TestExecutor, peer: PeerTask) -> PeerTask {
        let (_sender, receiver) = mpsc::channel(0);
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);
        exec.run_until_stalled(&mut run_fut)
            .expect_pending("shouldn't be done while _sender is live");
        drop(_sender);
        exec.run_until_stalled(&mut run_fut).unwrap()
    }

    async fn expect_sco_connection(
        profile_requests: &mut ProfileRequestStream,
        result: Result<(), ScoErrorCode>,
    ) -> Option<zx::Socket> {
        let proxy = match profile_requests.next().await.expect("request").unwrap() {
            bredr::ProfileRequest::ConnectSco { receiver, params, .. } => {
                // TODO(fxbug.dev/81374): Remove when mSBC is enabled again.
                let param_set = params.parameter_set.unwrap();
                assert!(param_set != bredr::HfpParameterSet::MsbcT1);
                assert!(param_set != bredr::HfpParameterSet::MsbcT2);
                receiver.into_proxy().unwrap()
            }
            x => panic!("Unexpected request to profile stream: {:?}", x),
        };
        match result {
            Ok(()) => {
                let (local, remote) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
                let connection =
                    bredr::ScoConnection { socket: Some(local), ..bredr::ScoConnection::EMPTY };
                proxy.connected(connection).unwrap();
                Some(remote)
            }
            Err(code) => {
                proxy.error(code).unwrap();
                None
            }
        }
    }

    /// Setup a new audio connection between the PeerTask and an upstream ProfileRequestStream.
    /// This helper function asserts that the SCO connection is created and that the audio
    /// connection has started up.
    #[track_caller]
    fn setup_audio(
        exec: &mut fasync::TestExecutor,
        peer: &mut PeerTask,
        profile_requests: &mut ProfileRequestStream,
    ) -> Option<fidl::Socket> {
        let audio_connection_fut = peer.setup_audio_connection(None);
        pin_mut!(audio_connection_fut);

        exec.run_until_stalled(&mut audio_connection_fut).expect_pending("shouldn't be done yet");

        // Expect a sco connection, and have it succeed.
        let (remote_sco, mut audio_connection_fut) =
            run_while(exec, audio_connection_fut, expect_sco_connection(profile_requests, Ok(())));

        let res = exec.run_until_stalled(&mut audio_connection_fut).expect("should be done");
        res.expect("should have started up okay");

        remote_sco
    }

    #[fuchsia::test]
    fn setup_audio_connection_connects_and_starts_audio() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        // SLC is connected at the start of the test.
        let (connection, _old_remote) = create_and_connect_slc();
        let (mut peer, _sender, _receiver, mut profile_requests) =
            setup_peer_task(Some(connection));

        assert!(peer.connection.connected());

        let audio_control = peer.audio_control.clone();

        let _remote_sco = {
            let audio_connection_fut = peer.setup_audio_connection(None);
            pin_mut!(audio_connection_fut);

            exec.run_until_stalled(&mut audio_connection_fut)
                .expect_pending("shouldn't be done yet");

            // Expect a sco connection, and have it succeed.
            let sco_complete_fut = expect_sco_connection(&mut profile_requests, Ok(()));
            pin_mut!(sco_complete_fut);
            let result = exec.run_singlethreaded(&mut futures::future::select(
                audio_connection_fut,
                sco_complete_fut,
            ));

            let (remote_sco, mut audio_connection_fut) = match result {
                Either::Right(r) => r,
                Either::Left(_) => panic!("Audio connection future shouldn't have finished"),
            };

            let res = exec.run_until_stalled(&mut audio_connection_fut).expect("should be done");
            res.expect("should have started up okay");

            remote_sco
        };

        // Should have started up the test audio control. Test by trying to start it again, it
        // should be an error.
        {
            let mut lock = audio_control.lock();
            let _ = lock
                .start(PeerId(0), bredr::ScoConnectionParameters::EMPTY)
                .expect_err("shouldn't be able to start, already started");
        }

        // Trying to start when a connection exists should be an error.
        let audio_connection_fut = peer.setup_audio_connection(None);
        pin_mut!(audio_connection_fut);
        let res = exec.run_until_stalled(&mut audio_connection_fut).expect("should be done");
        let _ = res.expect_err("can't start when already started");
    }

    #[fuchsia::test]
    fn audio_is_stopped_when_sco_connection_closes() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        // SLC is connected at the start of the test.
        let (connection, _old_remote) = create_and_connect_slc();
        let (mut peer, _sender, receiver, mut profile_requests) = setup_peer_task(Some(connection));

        assert!(peer.connection.connected());

        let audio_control = peer.audio_control.clone();

        let remote_sco = setup_audio(&mut exec, &mut peer, &mut profile_requests);

        // Should have started up the test audio control.
        {
            let mut lock = audio_control.lock();
            let _ = lock
                .start(PeerId(0), bredr::ScoConnectionParameters::EMPTY)
                .expect_err("shouldn't be able to start, already started");
        }

        // Set up the run task.
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);
        let _ = exec.run_until_stalled(&mut run_fut);

        drop(remote_sco);

        // Spin the run task to notice that the SCO connection has failed, and drop
        // the audio / stop the audio.
        let _ = exec.run_until_stalled(&mut run_fut);

        // Should have stopped the audio - check by trying to stop it again, it should be an error
        let mut lock = audio_control.lock();
        let _ = lock.stop().expect_err("should already be stopped");
    }

    #[fuchsia::test]
    fn sco_connection_closed_when_call_ends() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        // SLC is connected at the start of the test.
        let (connection, _old_remote) = create_and_connect_slc();
        let (mut peer, _sender, _receiver, mut profile_requests) =
            setup_peer_task(Some(connection));

        assert!(peer.connection.connected());

        let _remote_sco = setup_audio(&mut exec, &mut peer, &mut profile_requests);

        // Run the PeerTask to handle all audio setup tasks
        let peer = run_peer_until_stalled(&mut exec, peer);

        assert!(peer.active_sco.is_some());

        // Create the Call Manager side of a PeerHandler to send a call state update to the
        // `OngoingHeld` state in order to tear down the active sco connection.
        // Once call_manager does the work required by the test, it completes, returning items that
        // should be kept alive for the remainder of the test.
        async fn call_manager(
            stream: PeerHandlerRequestStream,
        ) -> (impl Stream<Item = PeerHandlerWatchNextCallResponder>, CallRequestStream) {
            let mut stream = wait_for_call_stream(stream).await;

            // Send the held call response
            let responder = stream.next().await.unwrap();
            let (client_end, call_stream) = fidl::endpoints::create_request_stream().unwrap();
            let next_call = NextCall {
                call: Some(client_end),
                remote: Some("1234567".to_string()),
                state: Some(CallState::OngoingHeld),
                direction: Some(CallDirection::MobileTerminated),
                ..NextCall::EMPTY
            };
            responder.send(next_call).expect("Successfully send call information");

            (stream, call_stream)
        }

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        let call_manager_fut = call_manager(stream);
        pin_mut!(call_manager_fut);

        let (mut sender, receiver) = mpsc::channel(0);
        // Wire up the HFP side of PeerHandler by passing the proxy into the PeerTask.
        let handle_fut = sender.send(PeerRequest::Handle(proxy));

        // Join the futures that are responsible for sending events _into_ the PeerTask.
        let join_fut = futures::future::join(call_manager_fut, handle_fut);

        // Create the run future to drive the PeerTask forward.
        let run_fut = peer.run(receiver);
        pin_mut!(run_fut);

        // Run until `join_fut` completes.
        let (_stream, mut run_fut) = run_while(&mut exec, run_fut, join_fut);

        // Make sure all work that is pending in the PeerTask `run` future completes,
        // forcing the future to complete and return the `PeerTask` object.
        assert!(exec.run_until_stalled(&mut run_fut).is_pending());
        // Force `run_fut` to complete by dropping the `sender`.
        drop(sender);
        let peer = exec.run_singlethreaded(run_fut);

        // The SCO connection should be removed after the PeerTask has handled the Terminated call
        // update.
        assert!(peer.active_sco.is_none());
    }
}
