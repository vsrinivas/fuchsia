// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![recursion_limit = "256"]

use {
    crate::inspect_types::{RemoteCapabilitiesInspect, RemotePeerInspect, StreamingInspectData},
    bt_a2dp_sink_metrics as metrics, bt_avdtp as avdtp,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_media::AUDIO_ENCODING_SBC,
    fuchsia_async as fasync,
    fuchsia_bluetooth::inspect::DebugExt,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_contrib::nodes::ManagedNode,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, FutureExt, StreamExt,
    },
    lazy_static::lazy_static,
    parking_lot::RwLock,
    std::{
        collections::hash_map::{self, Entry},
        collections::HashMap,
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

mod inspect_types;
mod player;

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
        if let Ok(_player) = player::Player::new(AUDIO_ENCODING_SBC.to_string()).await {
            let sbc_stream = avdtp::StreamEndpoint::new(
                SBC_SEID,
                avdtp::MediaType::Audio,
                avdtp::EndpointType::Sink,
                vec![
                    avdtp::ServiceCapability::MediaTransport,
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::new(AUDIO_CODEC_SBC),
                        // SBC Codec Specific Information Elements:
                        // These are the mandatory support in sink.
                        // Byte 0:
                        //  - Sampling Frequencies: 44.1kHz, 48.0kHz
                        //  - Channel modes: All (MONO, DUAL CHANNEL, STEREO, JOINT STEREO)
                        // Byte 1:
                        //  - Block length: all (4, 8, 12, 16)
                        //  - Subbands: all (4, 8)
                        //  - Allocation Method: all (SNR and loudness)
                        // Byte 2-3: Minimum and maximum bitpool value. This is just the minimum to the max.
                        // TODO(jamuraa): there should be a way to build this data in a structured way (bt-a2dp?)
                        codec_extra: vec![0x3F, 0xFF, 2, 250],
                    },
                ],
            )?;
            s.insert(sbc_stream, AUDIO_ENCODING_SBC.to_string());
        }
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

    /// Adds a stream, indexing it by the endoint id, associated with an encoding,
    /// replacing any other stream with the same endpoint id.
    fn insert(&mut self, stream: avdtp::StreamEndpoint, codec: String) {
        self.0.insert(stream.local_id().clone(), Stream::new(stream, codec));
    }

    /// Retrievees a mutable reference to the endpoint with the `id`.
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

/// Discovers any remote streams and reports their information to the log.
async fn discover_remote_streams(
    peer: Arc<avdtp::Peer>,
    remote_capabilities_inspect: RemoteCapabilitiesInspect,
) {
    let mut cobalt = get_cobalt_logger();
    let streams = peer.discover().await.expect("Failed to discover source streams");
    fx_log_info!("Discovered {} streams", streams.len());
    for info in streams {
        match peer.get_all_capabilities(info.id()).await {
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
    /// This function should be called whenever the remote assoiated with this peer opens an
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
    fn start_requests_task(&mut self, remotes: Arc<RwLock<RemotesMap>>, device_id: String) {
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

    let inspect = inspect::Inspector::new();
    let mut fs = ServiceFs::new();
    inspect.export(&mut fs);
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

    let remotes: Arc<RwLock<RemotesMap>> = Arc::new(RwLock::new(HashMap::new()));

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
                        // Spawn tasks to handle this remote
                        remote.start_requests_task(remotes.clone(), device_id);
                        fuchsia_async::spawn(discover_remote_streams(
                            remote.peer(),
                            remote.remote_capabilities_inspect(),
                        ));
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
}
