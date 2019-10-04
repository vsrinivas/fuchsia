// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    crate::inspect_types::{RemoteCapabilitiesInspect, RemotePeerInspect, StreamingInspectData},
    bt_a2dp::media_types::*,
    bt_a2dp_sink_metrics as metrics, bt_avdtp as avdtp,
    failure::{format_err, Error, ResultExt},
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
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_contrib::nodes::ManagedNode,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, FutureExt, StreamExt,
    },
    futures::{stream::TryStreamExt, TryFutureExt},
    lazy_static::lazy_static,
    parking_lot::RwLock,
    std::{
        collections::hash_map::{self, Entry},
        collections::HashMap,
        convert::TryFrom,
        string::String,
        sync::Arc,
    },
};

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

/// Controls a stream endpoint and the media decoding task which is associated with it.
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

    /// Attempt to start the media decoding task.
    fn start(&mut self, inspect: StreamingInspectData) -> Result<(), avdtp::ErrorCode> {
        let start_res = self.endpoint.start();
        if start_res.is_err() || self.suspend_sender.is_some() {
            fx_log_info!("Start when streaming: {:?} {:?}", start_res, self.suspend_sender);
            return Err(avdtp::ErrorCode::BadState);
        }
        let (send, receive) = mpsc::channel(1);
        self.suspend_sender = Some(send);

        fuchsia_async::spawn_local(decode_media_stream(
            self.endpoint.take_transport(),
            self.encoding.clone(),
            receive,
            inspect,
        ));
        Ok(())
    }

    /// Signals to the media decoding task to end.
    fn stop(&mut self) -> Result<(), avdtp::ErrorCode> {
        if let Err(e) = self.endpoint.suspend() {
            fx_log_info!("Stop when not streaming: {}", e);
            return Err(avdtp::ErrorCode::BadState);
        }
        match self.suspend_sender.take() {
            None => Err(avdtp::ErrorCode::BadState),
            Some(mut sender) => sender.try_send(()).or(Err(avdtp::ErrorCode::BadState)),
        }
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
struct Streams(HashMap<avdtp::StreamEndpointId, Stream>);

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
        if let Err(e) = player::Player::new(AUDIO_ENCODING_SBC.to_string()).await {
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
        )
        .expect("Couldn't create sbc media codec info.");
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

/// Determines if Peer profile version is newer (>= 1.3) or older (< 1.3)
fn a2dp_version_check(profile: ProfileDescriptor) -> bool {
    (profile.major_version == 1 && profile.minor_version >= 3) || profile.major_version > 1
}

/// Discovers any remote streams and reports their information to the log.
async fn discover_remote_streams(
    peer: Arc<avdtp::Peer>,
    remote_capabilities_inspect: RemoteCapabilitiesInspect,
    profile: Option<ProfileDescriptor>,
) {
    let mut cobalt = get_cobalt_logger();
    let streams = peer.discover().await.expect("Discover: Failed to discover source streams");
    fx_log_info!("Discovered {} streams", streams.len());
    for info in streams {
        if profile.is_none() {
            fx_log_info!("No profile information available.");
            return;
        }

        let profile = profile.expect("is not none");
        // Query get_all_capabilities if the A2DP version is >= 1.3
        let capabilities_fut = if a2dp_version_check(profile) {
            peer.get_all_capabilities(info.id()).await
        } else {
            peer.get_capabilities(info.id()).await
        };

        match capabilities_fut {
            Ok(capabilities) => {
                remote_capabilities_inspect.append(info.id(), &capabilities).await;
                fx_log_info!("Stream {:?}", info);
                for cap in capabilities {
                    fx_log_info!("  - {:?}", cap);
                    if let avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type,
                        ..
                    } = cap
                    {
                        let event_code = match codec_type {
                            avdtp::MediaCodecType::AUDIO_SBC => {
                                metrics::A2dpCodecAvailabilityMetricDimensionCodec::Sbc
                            }
                            _ => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Unknown,
                        };
                        cobalt.log_event(
                            metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID,
                            event_code as u32,
                        );
                    }
                }
            }
            Err(e) => fx_log_info!("Stream {} discovery failed: {:?}", info.id(), e),
        };
    }
}

/// RemotePeer handles requests from the AVDTP layer, and provides responses as appropriate based
/// on the current state of the A2DP streams available.
/// Each remote A2DP source interacts with its own set of stream endpoints.
struct RemotePeer {
    /// AVDTP peer communicating to this.
    peer: Arc<avdtp::Peer>,
    /// Some(id) if this peer has just opened a stream but the StreamEndpoint hasn't signaled the
    /// end of channel opening yet. Per AVDTP Sec 6.11 only up to one stream can be in this state.
    opening: Option<avdtp::StreamEndpointId>,
    /// The stream endpoint collection for this peer.
    streams: Streams,

    /// The inspect data for this peer.
    inspect: RemotePeerInspect,
}

type RemotesMap = HashMap<String, RemotePeer>;

// Profiles of AVDTP peers that have been discovered & connected
type ProfilesMap = HashMap<String, Option<ProfileDescriptor>>;

impl RemotePeer {
    fn new(peer: avdtp::Peer, mut streams: Streams, inspect: inspect::Node) -> RemotePeer {
        // Setup inspect nodes for the remote peer and for each of the streams that it holds
        let mut inspect = RemotePeerInspect::new(inspect);
        for (id, stream) in streams.iter_mut() {
            let stream_state_property = inspect.create_stream_state_inspect(id);
            let callback = move |stream: &avdtp::StreamEndpoint| {
                stream_state_property.set(&format!("{:?}", stream))
            };
            stream.set_endpoint_update_callback(Some(Box::new(callback)));
        }

        RemotePeer { peer: Arc::new(peer), opening: None, streams, inspect }
    }

    /// Provides a reference to the AVDTP peer.
    fn peer(&self) -> Arc<avdtp::Peer> {
        self.peer.clone()
    }

    fn remote_capabilities_inspect(&self) -> RemoteCapabilitiesInspect {
        self.inspect.remote_capabilities_inspect()
    }

    /// Provide a new established L2CAP channel to this remote peer.
    /// This function should be called whenever the remote associated with this peer opens an
    /// L2CAP channel after the first.
    fn receive_channel(&mut self, channel: zx::Socket) -> Result<(), Error> {
        let stream = match &self.opening {
            None => Err(format_err!("No stream opening.")),
            Some(id) => self.streams.get_endpoint(&id).ok_or(format_err!("endpoint doesn't exist")),
        }?;
        if !stream.receive_channel(fasync::Socket::from_socket(channel)?)? {
            self.opening = None;
        }
        fx_log_info!("connected transport channel to seid {}", stream.local_id());
        Ok(())
    }

    /// Start an asynchronous task to handle any requests from the AVDTP peer.
    /// This task completes when the remote end closes the signaling connection.
    /// This remote peer should be active in the `remotes` map with an id of `device_id`.
    /// When the signaling connection is closed, the task deactivates the remote, removing it
    /// from the `remotes` map.
    fn start_requests_task(
        &mut self,
        remotes: Arc<RwLock<RemotesMap>>,
        device_id: String,
        profiles: Arc<RwLock<ProfilesMap>>,
    ) {
        let mut request_stream = self.peer.take_request_stream();
        fuchsia_async::spawn(async move {
            while let Some(r) = request_stream.next().await {
                match r {
                    Err(e) => fx_log_info!("Request Error on {}: {:?}", device_id, e),
                    Ok(request) => {
                        let mut peer;
                        {
                            let mut wremotes = remotes.write();
                            peer = wremotes.remove(&device_id).expect("Can't get peer");
                        }
                        let fut = peer.handle_request(request);
                        if let Err(e) = fut.await {
                            fx_log_warn!("{} Error handling request: {:?}", device_id, e);
                        }
                        let replaced = remotes.write().insert(device_id.clone(), peer);
                        assert!(replaced.is_none(), "Two peers of {} connected", device_id);
                    }
                }
            }
            remotes.write().remove(&device_id);
            profiles.write().remove(&device_id);
            fx_log_info!("Peer {} disconnected", device_id);
        });
    }

    /// Handle a single request event from the avdtp peer.
    async fn handle_request(&mut self, r: avdtp::Request) -> avdtp::Result<()> {
        fx_vlog!(1, "Handling {:?} from peer..", r);
        match r {
            avdtp::Request::Discover { responder } => responder.send(&self.streams.information()),
            avdtp::Request::GetCapabilities { responder, stream_id }
            | avdtp::Request::GetAllCapabilities { responder, stream_id } => {
                match self.streams.get_endpoint(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => responder.send(stream.capabilities()),
                }
            }
            avdtp::Request::Open { responder, stream_id } => {
                match self.streams.get_endpoint(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => match stream.establish() {
                        Ok(()) => {
                            self.opening = Some(stream_id);
                            responder.send()
                        }
                        Err(_) => responder.reject(avdtp::ErrorCode::BadState),
                    },
                }
            }
            avdtp::Request::Close { responder, stream_id } => {
                match self.streams.get_endpoint(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream.release(responder, &self.peer).await,
                }
            }
            avdtp::Request::SetConfiguration {
                responder,
                local_stream_id,
                remote_stream_id,
                capabilities,
            } => {
                let stream = match self.streams.get_endpoint(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(BT-695): Confirm the MediaCodec parameters are OK
                match stream.configure(&remote_stream_id, capabilities.clone()) {
                    Ok(_) => responder.send(),
                    Err(e) => {
                        // Only happens when this is already configured.
                        responder.reject(None, avdtp::ErrorCode::SepInUse)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::GetConfiguration { stream_id, responder } => {
                let stream = match self.streams.get_endpoint(&stream_id) {
                    None => return responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                match stream.get_configuration() {
                    Ok(c) => responder.send(&c),
                    Err(e) => {
                        // Only happens when the stream is in the wrong state
                        responder.reject(avdtp::ErrorCode::BadState)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::Reconfigure { responder, local_stream_id, capabilities } => {
                let stream = match self.streams.get_endpoint(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(jamuraa): Actually tweak the codec parameters.
                match stream.reconfigure(capabilities) {
                    Ok(_) => responder.send(),
                    Err(e) => {
                        responder.reject(None, avdtp::ErrorCode::BadState)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::Start { responder, stream_ids } => {
                for seid in stream_ids {
                    let inspect = &mut self.inspect;
                    let res = self.streams.get_mut(&seid).and_then(|stream| {
                        let inspect = inspect.create_streaming_inspect_data(&seid);
                        stream.start(inspect)
                    });
                    if let Err(code) = res {
                        return responder.reject(&seid, code);
                    }
                }
                responder.send()
            }
            avdtp::Request::Suspend { responder, stream_ids } => {
                for seid in stream_ids {
                    if let Err(code) = self.streams.get_mut(&seid).and_then(|x| x.stop()) {
                        return responder.reject(&seid, code);
                    }
                }
                responder.send()
            }
            avdtp::Request::Abort { responder, stream_id } => {
                let stream = match self.streams.get_endpoint(&stream_id) {
                    None => return Ok(()),
                    Some(stream) => stream,
                };
                stream.abort(None).await?;
                self.opening = self.opening.take().filter(|id| id != &stream_id);
                let _ = self.streams.get_mut(&stream_id).and_then(|x| x.stop());
                responder.send()
            }
        }
    }

    async fn handle_controller_request(
        &mut self,
        request: PeerControllerRequest,
        peer_id: String,
        info: &avdtp::StreamInformation,
    ) -> Result<(), fidl::Error> {
        fx_log_info!("handle_controller_request for id: {:?}, {:?}", peer_id, info);
        match request {
            PeerControllerRequest::SetConfiguration { responder } => {
                let generic_capabilities = vec![avdtp::ServiceCapability::MediaTransport];
                match self.peer.set_configuration(info.id(), info.id(), &generic_capabilities).await
                {
                    Ok(resp) => fx_log_info!("SetConfiguration successful: {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} set_configuration failed: {:?}", info.id(), e);
                        match e {
                            avdtp::Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
                    }
                }
            }
            PeerControllerRequest::GetConfiguration { responder } => {
                match self.peer.get_configuration(info.id()).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetConfiguration: {:?}",
                            service_capabilities
                        );
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_configuration failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetCapabilities { responder } => {
                match self.peer.get_capabilities(info.id()).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetCapabilities {:?}",
                            service_capabilities
                        );
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_capabilities failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetAllCapabilities { responder } => {
                match self.peer.get_all_capabilities(info.id()).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetAllCapabilities: {:?}",
                            service_capabilities
                        );
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_all_capabilities failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendStream { responder } => {
                let suspend_vec = [info.id().clone()];
                match self.peer.suspend(&suspend_vec[..]).await {
                    Ok(resp) => fx_log_info!("SuspendStream was successful {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} suspend failed: {:?}", info.id(), e);
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
                match self.peer.reconfigure(info.id(), &generic_capabilities[..]).await {
                    Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} reconfigure failed: {:?}", info.id(), e);
                        match e {
                            avdtp::Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
                    }
                }
            }
            PeerControllerRequest::ReleaseStream { responder } => {
                match self.peer.close(info.id()).await {
                    Ok(resp) => fx_log_info!("ReleaseStream was successful: {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} release failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::EstablishStream { responder } => {
                match self.peer.open(info.id()).await {
                    Ok(resp) => fx_log_info!("EstablishStream was successful: {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} establish failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendAndReconfigure { responder } => {
                let suspend_vec = [info.id().clone()];
                match self.peer.suspend(&suspend_vec[..]).await {
                    Ok(resp) => {
                        fx_log_info!("Suspend was successful {:?}", resp);
                        // Only one frequency, channel mode, block length, subband,
                        // and allocation for reconfigure (A2DP 4.3.2)
                        let generic_capabilities = vec![avdtp::ServiceCapability::MediaCodec {
                            media_type: avdtp::MediaType::Audio,
                            codec_type: avdtp::MediaCodecType::new(AUDIO_CODEC_SBC),
                            codec_extra: vec![0x11, 0x15, 2, 250],
                        }];

                        match self.peer.reconfigure(info.id(), &generic_capabilities[..]).await {
                            Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
                            Err(e) => {
                                fx_log_info!("Stream {} reconfigure failed: {:?}", info.id(), e);
                                responder.send(&mut Err(PeerError::ProtocolError))?;
                            }
                        }
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} suspend failed: {:?}", info.id(), e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
        }

        Ok(())
    }
}

struct AvdtpController {
    fidl_stream: PeerControllerRequestStream,
    peer_id: String,
}

impl AvdtpController {
    fn new(fidl_stream: PeerControllerRequestStream, peer_id: String) -> Self {
        Self { fidl_stream, peer_id }
    }

    async fn route_fidl_requests(&mut self, remotes: Arc<RwLock<RemotesMap>>) -> Result<(), Error> {
        while let Some(req) = self.fidl_stream.next().await {
            let mut remote_peer;
            {
                let mut wremotes = remotes.write();
                remote_peer =
                    wremotes.remove(&self.peer_id).expect("Avdtp controller: Can't get peer");
            }
            let streams = remote_peer
                .peer
                .discover()
                .await
                .expect("avdtp: Failed to discover source streams");

            if !streams.is_empty() {
                // Only need to handle requests for one stream
                let info = &streams[0];
                let fut = remote_peer.handle_controller_request(req?, self.peer_id.clone(), info);
                if let Err(e) = fut.await {
                    fx_log_warn!("{} Error handling request: {:?}", self.peer_id, e);
                }
            }
            let replaced = remotes.write().insert(self.peer_id.clone(), remote_peer);
            assert!(replaced.is_none(), "Two peers of {} connected", self.peer_id);
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
    remotes: Arc<RwLock<RemotesMap>>,
    peer_id: String,
) {
    fx_log_info!("spawn_avdtp_peer_controller: {:?}.", peer_id);
    let remotes = remotes.clone();
    fasync::spawn(
        async move {
            let mut con = AvdtpController::new(fidl_stream, peer_id.clone());
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
    remotes: Arc<RwLock<RemotesMap>>,
) -> Result<(), failure::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerRequest::GetPeer { peer_id, handle, control_handle: _ } => {
                // Use id here to propagate throughout as key into RemoteMap.
                // If the id does not exist, close the channel as the peer has not been connected yet.
                // Zero-pad the id since we are converting from PeerId (u64) -> String
                let handle_to_client: fidl::endpoints::ServerEnd<PeerControllerMarker> = handle;
                let peer_id: PeerId = peer_id.clone().into();
                let peer_id_str = peer_id.to_string();
                fx_log_info!("GetPeer: Creating peer controller for peer with id {}.", peer_id_str);

                match handle_to_client.into_stream() {
                    Err(err) => fx_log_warn!(
                        "Error. Unable to create server endpoint from stream: {:?}.",
                        err
                    ),
                    Ok(client_stream) => {
                        spawn_avdtp_peer_controller(client_stream, remotes.clone(), peer_id_str)
                    }
                }
            }
            PeerManagerRequest::ConnectedPeers { responder } => {
                let mut connected_peers: Vec<fidl_fuchsia_bluetooth::PeerId> =
                    Vec::with_capacity(8);
                for peer in remotes.write().keys() {
                    let peer_id = PeerId::try_from(peer.clone()).expect("String to PeerId failed.");
                    let fidl_peer_id: fidl_fuchsia_bluetooth::PeerId = peer_id.into();
                    connected_peers.push(fidl_peer_id);
                }
                //let iter = connected_peers.into_iter();
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
    remotes: Arc<RwLock<RemotesMap>>,
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
    mut end_signal: Receiver<()>,
    mut inspect: StreamingInspectData,
) -> () {
    let mut player = match player::Player::new(encoding.clone()).await {
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
                    player = match player::Player::new(encoding.clone()).await {
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
        metrics::A2DP_NUMBER_OF_MICROSECONDS_STREAMED_METRIC_ID,
        metrics::A2dpNumberOfMicrosecondsStreamedMetricDimensionCodec::Sbc as u32,
        (end_time - start_time).into_micros(),
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");

    // Stateful objects for tracking connected peers
    let remotes: Arc<RwLock<RemotesMap>> = Arc::new(RwLock::new(HashMap::new()));
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

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

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
                let prof_desc: ProfileDescriptor = profile.clone();

                // Update the profile for the peer that is found
                // If the peer already exists, also run discovery for capabilities
                // Otherwise, only insert profile
                if let Some(_) = profiles.write().insert(peer_id.clone(), Some(prof_desc)) {
                    if let Entry::Occupied(mut entry) = remotes.write().entry(peer_id.clone()) {
                        let remote = entry.get_mut();
                        fuchsia_async::spawn(discover_remote_streams(
                            remote.peer(),
                            remote.remote_capabilities_inspect(),
                            Some(prof_desc.clone()),
                        ));
                    }
                }
            }
            Ok(ProfileEvent::OnConnected { device_id, service_id: _, channel, protocol }) => {
                fx_log_info!("Connection from {}: {:?} {:?}!", device_id, channel, protocol);
                match remotes.write().entry(device_id.clone()) {
                    Entry::Occupied(mut entry) => {
                        if let Err(e) = entry.get_mut().receive_channel(channel) {
                            fx_log_warn!("{} failed to connect channel: {}", device_id, e);
                        }
                    }
                    Entry::Vacant(entry) => {
                        fx_log_info!("Adding new peer for {}", device_id);
                        let peer = match avdtp::Peer::new(channel) {
                            Ok(peer) => peer,
                            Err(e) => {
                                fx_log_warn!("Error adding signaling peer {}: {:?}", device_id, e);
                                continue;
                            }
                        };
                        let inspect = inspect.root().create_child(format!("peer {}", device_id));
                        let remote = entry.insert(RemotePeer::new(peer, streams.clone(), inspect));

                        // Upon peer connected from ProfileEvent::OnConnected, send an event to the
                        // PeerManager event listener
                        let wpm_handle = pm_handle.clone();
                        if let Some(handle) = &wpm_handle.write().handle {
                            let peer_id: PeerId = PeerId::try_from(device_id.clone())
                                .expect("String to PeerId failed.");
                            if let Some(err) =
                                handle.send_on_peer_connected(&mut peer_id.into()).err()
                            {
                                fx_log_info!("Peer connected callback failed: {:?}", err);
                            }
                        }

                        // Spawn tasks to handle this remote
                        remote.start_requests_task(
                            remotes.clone(),
                            device_id.clone(),
                            profiles.clone(),
                        );

                        // Remote discovery only if profile information exists for the device_id
                        match profiles.write().entry(device_id.clone()) {
                            Entry::Occupied(entry) => {
                                if let Some(prof) = entry.get() {
                                    fuchsia_async::spawn(discover_remote_streams(
                                        remote.peer(),
                                        remote.remote_capabilities_inspect(),
                                        Some(prof.clone()),
                                    ));
                                }
                            }
                            // Otherwise just insert the device ID with no profile
                            // Run discovery when profile is updated
                            Entry::Vacant(entry) => {
                                entry.insert(None);
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

    #[test]
    /// Test that the Streams specialized hashmap works as expected, storing
    /// the stream based on the SEID and retrieving the right pieces from
    /// the accessors.
    fn test_streams() {
        let mut streams = Streams::new();

        // An endpoint for testing
        let s = avdtp::StreamEndpoint::new(
            1,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![avdtp::ServiceCapability::MediaTransport],
        )
        .unwrap();

        let id = s.local_id().clone();
        let information = s.information();
        let encoding = AUDIO_ENCODING_SBC.to_string();

        assert!(streams.get_endpoint(&id).is_none());

        let res = streams.get_mut(&id);

        assert!(res.is_err());
        assert_eq!(avdtp::ErrorCode::BadAcpSeid, res.err().unwrap());

        streams.insert(s, encoding.clone());

        assert!(streams.get_endpoint(&id).is_some());
        assert_eq!(&id, streams.get_endpoint(&id).unwrap().local_id());

        assert_eq!([information], streams.information().as_slice());

        let res = streams.get_mut(&id);

        assert!(res.as_ref().unwrap().suspend_sender.is_none());
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

    #[test]
    /// Test if the version check correctly returns the flag
    fn test_a2dp_version_check() {
        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        let res = a2dp_version_check(p1);

        assert_eq!(true, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 10,
        };
        let res = a2dp_version_check(p1);

        assert_eq!(true, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 0,
        };
        let res = a2dp_version_check(p1);

        assert_eq!(false, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 0,
            minor_version: 9,
        };
        let res = a2dp_version_check(p1);

        assert_eq!(false, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 2,
        };
        let res = a2dp_version_check(p1);

        assert_eq!(true, res);
    }
}
