// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    bt_a2dp::media_types::*,
    bt_a2dp_sink_metrics as metrics, bt_avdtp as avdtp,
    failure::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_avdtp::{
        PeerControllerMarker, PeerControllerRequest, PeerControllerRequestStream, PeerError,
        PeerManagerRequest, PeerManagerRequestStream,
    },
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_media::AUDIO_ENCODING_SBC,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{inspect::DebugExt, types::PeerId},
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::nodes::ManagedNode,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, Future, FutureExt, StreamExt,
    },
    futures::{stream::TryStreamExt, TryFutureExt},
    lazy_static::lazy_static,
    parking_lot::RwLock,
    std::{
        collections::hash_map::{self, Entry},
        collections::{HashMap, HashSet},
        string::String,
        sync::Arc,
    },
};

use crate::inspect_types::StreamingInspectData;

lazy_static! {
    /// COBALT_SENDER must only be accessed from within an async context;
    static ref COBALT_SENDER: CobaltSender = {
        let (sender, reporter) = CobaltConnector::default().serve(ConnectionType::project_name("bluetooth"));
        fasync::spawn(reporter);
        sender
    };
}

fn get_cobalt_logger() -> CobaltSender {
    COBALT_SENDER.clone()
}

use crate::types::ControlHandleManager;

mod inspect_types;
mod peer;
mod player;
mod types;

/// Make the SDP definition for the A2DP sink service.
fn make_profile_service_definition() -> ServiceDefinition {
    ServiceDefinition {
        service_class_uuids: vec![String::from("110B")], // Audio Sink UUID
        protocol_descriptors: vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(PSM_AVDTP),
                }],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avdtp,
                params: vec![DataElement {
                    type_: DataElementType::UnsignedInteger,
                    size: 2,
                    data: DataElementData::Integer(0x0103), // Indicate v1.3
                }],
            },
        ],
        profile_descriptors: vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        }],
        additional_protocol_descriptors: None,
        information: vec![Information {
            language: "en".to_string(),
            name: Some("A2DP".to_string()),
            description: Some("Advanced Audio Distribution Profile".to_string()),
            provider: Some("Fuchsia".to_string()),
        }],
        additional_attributes: None,
    }
}

// SDP Attribute ID for the Supported Features of A2DP.
// Defined in Assigned Numbers for SDP
// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_A2DP_SUPPORTED_FEATURES: u16 = 0x0311;

// Defined in the Bluetooth Assigned Numbers for Audio/Video applications
// https://www.bluetooth.com/specifications/assigned-numbers/audio-video
const AUDIO_CODEC_SBC: u8 = 0;
// Arbitrarily chosen ID for the SBC stream endpoint.
const SBC_SEID: u8 = 6;

const DEFAULT_SAMPLE_RATE: u32 = 48000;

/// Controls a stream endpoint and the media decoding task which is associated with it.
#[derive(Debug)]
struct Stream {
    /// The AVDTP endpoint that this stream is associated with.
    endpoint: avdtp::StreamEndpoint,
    /// The encoding that media sent to this endpoint should be encoded with.
    /// This should be an encoding constant from fuchsia.media like AUDIO_ENCODING_SBC.
    /// See //sdk/fidl/fuchsia.media/stream_type.fidl for valid encodings.
    encoding: String,
    /// Some(sender) when a stream task is started.  Signaling on this sender will
    /// end the media streaming task.
    suspend_sender: Option<Sender<()>>,
}

impl Stream {
    fn new(endpoint: avdtp::StreamEndpoint, encoding: String) -> Stream {
        Stream { endpoint, encoding, suspend_sender: None }
    }

    /// Extract sampling freqency from SBC codec extra data field
    /// (A2DP Sec. 4.3.2)
    fn parse_sbc_sampling_frequency(codec_extra: &[u8]) -> u32 {
        if codec_extra.len() != 4 {
            fx_log_warn!("Invalid SBC codec extra length: {:?}", codec_extra.len());
            return DEFAULT_SAMPLE_RATE;
        }

        let mut codec_info_bytes = [0_u8; 4];
        codec_info_bytes.copy_from_slice(&codec_extra);

        let codec_info = SbcCodecInfo(u32::from_be_bytes(codec_info_bytes));
        let sample_freq = SbcSamplingFrequency::from_bits_truncate(codec_info.sampling_frequency());

        match sample_freq {
            SbcSamplingFrequency::FREQ48000HZ => 48000,
            SbcSamplingFrequency::FREQ44100HZ => 44100,
            _ => {
                fx_log_warn!("Invalid sample_freq set in configuration {:?}", sample_freq);
                DEFAULT_SAMPLE_RATE
            }
        }
    }

    /// Get the currently configured sampling frequency for this stream or return a default value
    /// if none is configured.
    ///
    /// TODO: This should be removed once we have a structured way of accessing stream
    /// capabilities.
    fn sample_freq(&self) -> u32 {
        self.endpoint
            .get_configuration()
            .map(|caps| {
                for c in caps {
                    if let avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        codec_extra,
                    } = c
                    {
                        return Self::parse_sbc_sampling_frequency(&codec_extra);
                    }
                }
                DEFAULT_SAMPLE_RATE
            })
            .unwrap_or(DEFAULT_SAMPLE_RATE)
    }

    /// Attempt to start the media decoding task.
    fn start(&mut self, inspect: StreamingInspectData) -> avdtp::Result<()> {
        let start_res = self.endpoint.start();
        if start_res.is_err() || self.suspend_sender.is_some() {
            fx_log_info!("Start when streaming: {:?} {:?}", start_res, self.suspend_sender);
            return Err(avdtp::Error::InvalidState);
        }
        let (send, receive) = mpsc::channel(1);
        self.suspend_sender = Some(send);

        fuchsia_async::spawn_local(decode_media_stream(
            self.endpoint.take_transport()?,
            self.encoding.clone(),
            self.sample_freq(),
            receive,
            inspect,
        ));
        Ok(())
    }

    /// Signals to the media decoding task to end.
    fn stop(&mut self) -> avdtp::Result<()> {
        self.endpoint.suspend()?;
        let mut sender = self.suspend_sender.take().ok_or(avdtp::Error::InvalidState)?;
        sender.try_send(()).or(Err(avdtp::Error::InvalidState))
    }

    /// Pass update callback to StreamEndpoint that will be called anytime a `StreamEndpoint` is
    /// modified.
    /// Passing in a value of `None` removes the callback.
    fn set_endpoint_update_callback(
        &mut self,
        callback: Option<avdtp::StreamEndpointUpdateCallback>,
    ) {
        self.endpoint.set_update_callback(callback)
    }
}

impl Clone for Stream {
    fn clone(&self) -> Self {
        Stream {
            endpoint: self.endpoint.as_new(),
            encoding: self.encoding.clone(),
            suspend_sender: None,
        }
    }
}

/// A collection of streams that can be indexed by their EndpointId to their
/// endpoint and the codec to use for this endpoint.
#[derive(Clone)]
pub(crate) struct Streams(HashMap<avdtp::StreamEndpointId, Stream>);

impl Streams {
    /// A new empty set of endpoints.
    fn new() -> Streams {
        Streams(HashMap::new())
    }

    /// Builds a set of endpoints from the available codecs.
    async fn build(inspect: &mut ManagedNode) -> Result<Streams, Error> {
        let mut s = Streams::new();
        // TODO(BT-533): detect codecs, add streams for each codec
        // SBC is required
        if let Err(e) =
            player::Player::new(AUDIO_ENCODING_SBC.to_string(), DEFAULT_SAMPLE_RATE).await
        {
            fx_log_warn!("Can't play required SBC audio: {}", e);
            return Err(e);
        }

        let sbc_media_codec_info: SbcCodecInfo = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )?;
        fx_log_info!("Supported codec parameters: {:?}.", sbc_media_codec_info);

        let sbc_stream = avdtp::StreamEndpoint::new(
            SBC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::new(AUDIO_CODEC_SBC),
                    codec_extra: sbc_media_codec_info.to_bytes(),
                },
            ],
        )?;
        s.insert(sbc_stream, AUDIO_ENCODING_SBC.to_string());

        s.construct_inspect_data(inspect);
        Ok(s)
    }

    /// Constructs an inspect tree enumerating all local stream endpoints with encoding and
    /// capability properties. The values in this tree are static and represent `Streams` inspect
    /// data at the point in time when `Streams` was built.
    ///
    /// This function should be called by `Streams::build` as part of construction.
    fn construct_inspect_data(&self, inspect: &mut ManagedNode) {
        let mut writer = inspect.writer();
        for stream in self.0.values() {
            let id = stream.endpoint.local_id();
            let capabilities = stream.endpoint.capabilities();
            let mut writer = writer.create_child(&format!("stream {}", id));
            writer.create_string("encoding", &stream.encoding);
            writer.create_string("capabilities", capabilities.debug());
        }
    }

    /// Adds a stream, indexing it by the endpoint id, associated with an encoding,
    /// replacing any other stream with the same endpoint id.
    fn insert(&mut self, stream: avdtp::StreamEndpoint, codec: String) {
        self.0.insert(stream.local_id().clone(), Stream::new(stream, codec));
    }

    /// Retrieves a mutable reference to the endpoint with the `id`.
    fn get_endpoint(&mut self, id: &avdtp::StreamEndpointId) -> Option<&mut avdtp::StreamEndpoint> {
        self.0.get_mut(id).map(|x| &mut x.endpoint)
    }

    /// Retrieves a mutable reference to the Stream referenced by `id`, if the stream exists,
    /// otherwise returns Err(BadAcpSeid).
    fn get_mut(&mut self, id: &avdtp::StreamEndpointId) -> Result<&mut Stream, avdtp::ErrorCode> {
        self.0.get_mut(id).ok_or(avdtp::ErrorCode::BadAcpSeid)
    }

    /// Returns the information on all known streams.
    fn information(&self) -> Vec<avdtp::StreamInformation> {
        self.0.values().map(|x| x.endpoint.information()).collect()
    }

    /// Gives a count of how many streams are currently registered.
    fn len(&self) -> usize {
        self.0.len()
    }

    fn iter_mut(&mut self) -> hash_map::IterMut<avdtp::StreamEndpointId, Stream> {
        self.0.iter_mut()
    }
}

fn codectype_to_availability_metric(
    codec_type: avdtp::MediaCodecType,
) -> metrics::A2dpCodecAvailabilityMetricDimensionCodec {
    match codec_type {
        avdtp::MediaCodecType::AUDIO_SBC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Sbc,
        avdtp::MediaCodecType::AUDIO_MPEG12 => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::Mpeg12
        }
        avdtp::MediaCodecType::AUDIO_AAC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Aac,
        avdtp::MediaCodecType::AUDIO_ATRAC => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::Atrac
        }
        avdtp::MediaCodecType::AUDIO_NON_A2DP => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::VendorSpecific
        }
        _ => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Unknown,
    }
}

/// Discovers any remote streams and reports their information to the log.
fn discover_remote_streams(peer: &peer::Peer) -> impl Future<Output = ()> {
    let collect_fut = peer.collect_capabilities();
    let remote_capabilities_inspect = peer.remote_capabilities_inspect();

    async move {
        let mut cobalt = get_cobalt_logger();
        // Store deduplicated set of codec event codes for logging.
        let mut codec_event_codes = HashSet::new();

        let streams = match collect_fut.await {
            Ok(streams) => streams,
            Err(e) => {
                fx_log_info!("Collecting capabilities failed: {:?}", e);
                return;
            }
        };

        for stream in streams {
            let capabilities = stream.capabilities();
            remote_capabilities_inspect.append(stream.local_id(), &capabilities).await;
            for cap in capabilities {
                if let avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type,
                    ..
                } = cap
                {
                    codec_event_codes
                        .insert(codectype_to_availability_metric(codec_type.clone()) as u32);
                }
            }
        }

        for event_code in codec_event_codes {
            cobalt.log_event(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event_code);
        }
    }
}

type PeerMap = HashMap<PeerId, peer::Peer>;

// Profiles of AVDTP peers that have been discovered but not connected
type ProfilesMap = HashMap<PeerId, Option<ProfileDescriptor>>;

async fn handle_controller_request(
    avdtp: Arc<avdtp::Peer>,
    request: PeerControllerRequest,
    endpoint_id: &avdtp::StreamEndpointId,
) -> Result<(), fidl::Error> {
    match request {
        PeerControllerRequest::SetConfiguration { responder } => {
            let generic_capabilities = vec![avdtp::ServiceCapability::MediaTransport];
            match avdtp.set_configuration(endpoint_id, endpoint_id, &generic_capabilities).await {
                Ok(resp) => fx_log_info!("SetConfiguration successful: {:?}", resp),
                Err(e) => {
                    fx_log_info!("Stream {} set_configuration failed: {:?}", endpoint_id, e);
                    match e {
                        avdtp::Error::RemoteConfigRejected(_, _) => {}
                        _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                    }
                }
            }
        }
        PeerControllerRequest::GetConfiguration { responder } => {
            match avdtp.get_configuration(endpoint_id).await {
                Ok(service_capabilities) => {
                    fx_log_info!(
                        "Service capabilities from GetConfiguration: {:?}",
                        service_capabilities
                    );
                }
                Err(e) => {
                    fx_log_info!("Stream {} get_configuration failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::GetCapabilities { responder } => {
            match avdtp.get_capabilities(endpoint_id).await {
                Ok(service_capabilities) => {
                    fx_log_info!(
                        "Service capabilities from GetCapabilities {:?}",
                        service_capabilities
                    );
                }
                Err(e) => {
                    fx_log_info!("Stream {} get_capabilities failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::GetAllCapabilities { responder } => {
            match avdtp.get_all_capabilities(endpoint_id).await {
                Ok(service_capabilities) => {
                    fx_log_info!(
                        "Service capabilities from GetAllCapabilities: {:?}",
                        service_capabilities
                    );
                }
                Err(e) => {
                    fx_log_info!("Stream {} get_all_capabilities failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::SuspendStream { responder } => {
            let suspend_vec = [endpoint_id.clone()];
            match avdtp.suspend(&suspend_vec[..]).await {
                Ok(resp) => fx_log_info!("SuspendStream was successful {:?}", resp),
                Err(e) => {
                    fx_log_info!("Stream {} suspend failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::ReconfigureStream { responder } => {
            // Only one frequency, channel mode, block length, subband,
            // and allocation for reconfigure (A2DP 4.3.2)
            let generic_capabilities = vec![avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::new(AUDIO_CODEC_SBC),
                codec_extra: vec![0x11, 0x15, 2, 250],
            }];
            match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
                Err(e) => {
                    fx_log_info!("Stream {} reconfigure failed: {:?}", endpoint_id, e);
                    match e {
                        avdtp::Error::RemoteConfigRejected(_, _) => {}
                        _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                    }
                }
            }
        }
        PeerControllerRequest::ReleaseStream { responder } => {
            match avdtp.close(endpoint_id).await {
                Ok(resp) => fx_log_info!("ReleaseStream was successful: {:?}", resp),
                Err(e) => {
                    fx_log_info!("Stream {} release failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::EstablishStream { responder } => {
            match avdtp.open(endpoint_id).await {
                Ok(resp) => fx_log_info!("EstablishStream was successful: {:?}", resp),
                Err(e) => {
                    fx_log_info!("Stream {} establish failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
        PeerControllerRequest::SuspendAndReconfigure { responder } => {
            let suspend_vec = [endpoint_id.clone()];
            match avdtp.suspend(&suspend_vec[..]).await {
                Ok(resp) => {
                    fx_log_info!("Suspend was successful {:?}", resp);
                    // Only one frequency, channel mode, block length, subband,
                    // and allocation for reconfigure (A2DP 4.3.2)
                    let generic_capabilities = vec![avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::new(AUDIO_CODEC_SBC),
                        codec_extra: vec![0x11, 0x15, 2, 250],
                    }];

                    match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                        Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
                        Err(e) => {
                            fx_log_info!("Stream {} reconfigure failed: {:?}", endpoint_id, e);
                            responder.send(&mut Err(PeerError::ProtocolError))?;
                        }
                    }
                }
                Err(e) => {
                    fx_log_info!("Stream {} suspend failed: {:?}", endpoint_id, e);
                    responder.send(&mut Err(PeerError::ProtocolError))?;
                }
            }
        }
    }

    Ok(())
}

struct AvdtpController {
    fidl_stream: PeerControllerRequestStream,
    peer_id: PeerId,
}

impl AvdtpController {
    fn new(fidl_stream: PeerControllerRequestStream, peer_id: PeerId) -> Self {
        Self { fidl_stream, peer_id }
    }

    async fn route_fidl_requests(&mut self, remotes: Arc<RwLock<PeerMap>>) -> Result<(), Error> {
        let mut streams = None;
        while let Some(req) = self.fidl_stream.next().await {
            let avdtp_peer = {
                let read = remotes.read();
                read.get(&self.peer_id).ok_or(avdtp::Error::PeerDisconnected)?.avdtp_peer()
            };

            if streams.is_none() {
                // Discover the streams
                streams = Some(avdtp_peer.discover().await?);
            }

            // Only need to handle requests for one stream
            let info = match streams.as_ref().expect("should be some").first() {
                Some(stream_info) => stream_info,
                None => {
                    // Try to discover again next time.
                    streams = None;
                    continue;
                }
            };

            fx_log_info!("handle_controller_request for id: {:?}, {:?}", self.peer_id, info);
            let fut = handle_controller_request(avdtp_peer, req?, info.id());
            if let Err(e) = fut.await {
                fx_log_warn!("{} Error handling request: {:?}", self.peer_id, e);
            }
        }
        fx_log_info!(
            "route_fidl_requests: Finished processing input stream with id: {:?}",
            self.peer_id
        );
        Ok(())
    }
}

fn spawn_avdtp_peer_controller(
    fidl_stream: PeerControllerRequestStream,
    remotes: Arc<RwLock<PeerMap>>,
    peer_id: PeerId,
) {
    fx_log_info!("spawn_avdtp_peer_controller: {:?}.", peer_id);
    let remotes = remotes.clone();
    fasync::spawn(
        async move {
            let mut con = AvdtpController::new(fidl_stream, peer_id);
            con.route_fidl_requests(remotes).await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

/// Control implementation to handle fidl requests.
/// State is stored in the remotes object.
async fn start_control_service(
    mut stream: PeerManagerRequestStream,
    remotes: Arc<RwLock<PeerMap>>,
) -> Result<(), failure::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerRequest::GetPeer { peer_id, handle, control_handle: _ } => {
                // Use id here to propagate throughout as key into PeerMap.
                // If the id does not exist, close the channel as the peer has not been connected yet.
                let handle_to_client: fidl::endpoints::ServerEnd<PeerControllerMarker> = handle;
                let peer_id: PeerId = peer_id.into();
                fx_log_info!("GetPeer: Creating peer controller for peer with id {}.", peer_id);

                match handle_to_client.into_stream() {
                    Err(err) => fx_log_warn!(
                        "Error. Unable to create server endpoint from stream: {:?}.",
                        err
                    ),
                    Ok(client_stream) => {
                        spawn_avdtp_peer_controller(client_stream, remotes.clone(), peer_id)
                    }
                }
            }
            PeerManagerRequest::ConnectedPeers { responder } => {
                let mut connected_peers: Vec<fidl_fuchsia_bluetooth::PeerId> =
                    remotes.read().keys().cloned().map(Into::into).collect();
                responder.send(&mut connected_peers.iter_mut())?;
                fx_log_info!("ConnectedPeers request. Peers: {:?}", connected_peers);
            }
        }
    }
    // event_listener will now be dropped, closing the listener.
    Ok(())
}

fn control_service(
    stream: PeerManagerRequestStream,
    remotes: Arc<RwLock<PeerMap>>,
    pm_handle: Arc<RwLock<ControlHandleManager>>,
) {
    // Before we start, save the control handle to the manager.
    // This allows the a2dp component to notify the client for the OnPeerConnected event.
    let control_handle = stream.control_handle().clone();
    pm_handle.write().insert(control_handle);

    fasync::spawn(
        start_control_service(stream, remotes.clone())
            .unwrap_or_else(|e| fx_log_err!("Failed to spawn {:?}", e)),
    )
}

/// Decodes a media stream by starting a Player and transferring media stream packets from AVDTP
/// to the player.  Restarts the player on player errors.
/// Ends when signaled from `end_signal`, or when the media transport stream is closed.
async fn decode_media_stream(
    mut stream: avdtp::MediaStream,
    encoding: String,
    sample_rate: u32,
    mut end_signal: Receiver<()>,
    mut inspect: StreamingInspectData,
) -> () {
    let mut player = match player::Player::new(encoding.clone(), sample_rate).await {
        Ok(v) => v,
        Err(e) => {
            fx_log_info!("Can't setup stream source for Media: {:?}", e);
            return;
        }
    };

    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    inspect.stream_started();
    loop {
        select! {
            item = stream.next().fuse() => {
                if item.is_none() {
                    fx_log_info!("Media transport closed");
                    break;
                }
                match item.unwrap() {
                    Ok(pkt) => {
                        match player.push_payload(&pkt.as_slice()) {
                            Err(e) => {
                                fx_log_info!("can't push packet: {:?}", e);
                            }
                            _ => (),
                        };
                        if !player.playing() {
                            player.play().unwrap_or_else(|e| fx_log_info!("Problem playing: {:}", e));
                        }
                        inspect.accumulated_bytes += pkt.len() as u64;
                    }
                    Err(e) => {
                        fx_log_info!("Error in media stream: {:?}", e);
                        break;
                    }
                }
            },
            evt = player.next_event().fuse() => {
                fx_log_info!("Player Event happened: {:?}", evt);
                if evt.is_none() {
                    fx_log_info!("Rebuilding Player: {:?}", evt);
                    // The player died somehow? Attempt to rebuild the player.
                    player = match player::Player::new(encoding.clone(), sample_rate).await {
                        Ok(v) => v,
                        Err(e) => {
                            fx_log_info!("Can't rebuild player: {:?}", e);
                            break;
                        }
                    };
                }
            }
            _ = inspect.update_interval.next() => {
                inspect.update_rx_statistics();
            }
            _ = end_signal.next().fuse() => {
                fx_log_info!("Stream ending on end signal");
                break;
            }
        }
    }
    let end_time = zx::Time::get(zx::ClockId::Monotonic);
    // TODO (BT-818): determine codec metric dimension from encoding instead of hard-coding to sbc
    get_cobalt_logger().log_elapsed_time(
        metrics::A2DP_NUMBER_OF_SECONDS_STREAMED_METRIC_ID,
        metrics::A2dpNumberOfSecondsStreamedMetricDimensionCodec::Sbc as u32,
        (end_time - start_time).into_seconds(),
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");

    // Stateful objects for tracking connected peers
    let remotes: Arc<RwLock<PeerMap>> = Arc::new(RwLock::new(HashMap::new()));
    let profiles: Arc<RwLock<ProfilesMap>> = Arc::new(RwLock::new(HashMap::new()));
    let pm_control_handle: Arc<RwLock<ControlHandleManager>> =
        Arc::new(RwLock::new(ControlHandleManager::new()));

    // Copy of state for pm = PeerManager control listener
    let pm_remotes = remotes.clone();
    let pm_handle = pm_control_handle.clone();

    let inspect = inspect::Inspector::new();
    let mut fs = ServiceFs::new();
    inspect.export(&mut fs);
    fs.dir("svc").add_fidl_service(move |s| {
        control_service(s, pm_remotes.clone(), pm_control_handle.clone())
    });
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_warn!("Unable to serve Inspect service directory: {}", e);
    }
    fasync::spawn(fs.collect::<()>());

    let mut stream_inspect =
        ManagedNode::new(inspect.root().create_child("local stream endpoints"));
    let streams = Streams::build(&mut stream_inspect).await?;

    if streams.len() == 0 {
        return Err(format_err!("Can't play media - no codecs found or media player missing"));
    }

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()?;

    let mut service_def = make_profile_service_definition();
    let (status, service_id) =
        profile_svc.add_service(&mut service_def, SecurityLevel::EncryptionOptional, false).await?;

    let attrs: Vec<u16> = vec![
        ATTR_PROTOCOL_DESCRIPTOR_LIST,
        ATTR_SERVICE_CLASS_ID_LIST,
        ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];
    profile_svc.add_search(ServiceClassProfileIdentifier::AudioSource, &mut attrs.into_iter())?;

    fx_log_info!("Registered Service ID {}", service_id);

    if let Some(e) = status.error {
        return Err(format_err!("Couldn't add A2DP sink service: {:?}", e));
    }

    let mut evt_stream = profile_svc.take_event_stream();
    while let Some(evt) = evt_stream.next().await {
        match evt {
            Err(e) => return Err(e.into()),
            Ok(ProfileEvent::OnServiceFound { peer_id, profile, attributes }) => {
                fx_log_info!(
                    "Audio Source on {} with profile {:?}: {:?}",
                    peer_id,
                    profile,
                    attributes
                );
                let peer_id = peer_id.parse().expect("peer ids from profile should parse");
                let prof_desc: ProfileDescriptor = profile.clone();

                // Update the profile for the peer that is found
                // If the peer already exists, also run discovery for capabilities
                // Otherwise, only insert profile
                if let Some(_) = profiles.write().insert(peer_id, Some(prof_desc)) {
                    if let Entry::Occupied(mut entry) = remotes.write().entry(peer_id) {
                        let remote = entry.get_mut();
                        remote.set_descriptor(prof_desc);
                        let discovery_fut = discover_remote_streams(remote);
                        fuchsia_async::spawn(discovery_fut);
                    }
                }
            }
            Ok(ProfileEvent::OnConnected { device_id, service_id: _, channel, protocol }) => {
                fx_log_info!("Connection from {}: {:?} {:?}!", device_id, channel, protocol);
                let peer_id = device_id.parse().expect("peer ids from profile should parse");
                match remotes.write().entry(peer_id) {
                    Entry::Occupied(mut entry) => {
                        if let Err(e) = entry.get_mut().receive_channel(channel) {
                            fx_log_warn!("{} failed to connect channel: {}", peer_id, e);
                        }
                    }
                    Entry::Vacant(entry) => {
                        fx_log_info!("Adding new peer for {}", peer_id);
                        let avdtp_peer = match avdtp::Peer::new(channel) {
                            Ok(peer) => peer,
                            Err(e) => {
                                fx_log_warn!("Error adding signaling peer {}: {:?}", peer_id, e);
                                continue;
                            }
                        };
                        let inspect = inspect.root().create_child(format!("peer {}", peer_id));
                        let mut peer =
                            peer::Peer::create(peer_id, avdtp_peer, streams.clone(), inspect);

                        // Start remote discovery if profile information exists for the device_id
                        match profiles.write().entry(peer_id) {
                            Entry::Occupied(entry) => {
                                if let Some(prof) = entry.get() {
                                    peer.set_descriptor(prof.clone());
                                    let discovery_fut = discover_remote_streams(&peer);
                                    fuchsia_async::spawn(discovery_fut);
                                }
                            }
                            // Otherwise just insert the device ID with no profile
                            // Run discovery when profile is updated
                            Entry::Vacant(entry) => {
                                entry.insert(None);
                            }
                        }

                        entry.insert(peer);

                        // Upon peer connected from ProfileEvent::OnConnected, send an event to the
                        // PeerManager event listener
                        let wpm_handle = pm_handle.write();
                        if let Some(handle) = &wpm_handle.handle {
                            if let Err(e) = handle.send_on_peer_connected(&mut peer_id.into()) {
                                fx_log_info!("Peer connected callback failed: {:?}", e);
                            }
                        }
                    }
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    /// Test that the Streams specialized hashmap works as expected, storing
    /// the stream based on the SEID and retrieving the right pieces from
    /// the accessors.
    fn test_streams() {
        let mut streams = Streams::new();
        const LOCAL_ID: u8 = 1;

        // An endpoint for testing
        let s = avdtp::StreamEndpoint::new(
            LOCAL_ID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: vec![41, 245, 2, 53],
                },
            ],
        )
        .expect("Failed to create endpoint");

        let id = s.local_id().clone();
        let information = s.information();
        let encoding = AUDIO_ENCODING_SBC.to_string();
        let sample_freq = 44100;

        assert_matches!(streams.get_endpoint(&id), None);

        let res = streams.get_mut(&id);

        assert_matches!(res, Err(avdtp::ErrorCode::BadAcpSeid));

        streams.insert(s, encoding.clone());

        assert!(streams.get_endpoint(&id).is_some());
        assert_eq!(&id, streams.get_endpoint(&id).unwrap().local_id());

        assert_eq!([information], streams.information().as_slice());

        streams
            .get_endpoint(&id)
            .unwrap()
            .configure(
                &id,
                vec![
                    avdtp::ServiceCapability::MediaTransport,
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        codec_extra: vec![41, 245, 2, 53],
                    },
                ],
            )
            .expect("Failed to configure endpoint");

        assert_eq!(sample_freq, streams.get_mut(&id).unwrap().sample_freq());

        let res = streams.get_mut(&id);

        assert_matches!(res.as_ref().unwrap().suspend_sender, None);
        assert_eq!(encoding, res.as_ref().unwrap().encoding);
    }

    #[test]
    /// Streams::build should fail because it can't start the SBC encoder, because MediaPlayer
    /// isn't available in the test environment.
    fn test_sbc_unavailable_error() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        let inspect = inspect::Inspector::new();
        let mut stream_inspect =
            ManagedNode::new(inspect.root().create_child("local stream endpoints"));
        let mut streams_fut = Box::pin(Streams::build(&mut stream_inspect));

        let streams = exec.run_singlethreaded(&mut streams_fut);

        assert!(streams.is_err(), "Stream building should fail when it can't reach MediaPlayer");
    }
}
