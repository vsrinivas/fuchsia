// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    async_helpers::component_lifecycle::ComponentLifecycleServer,
    bt_a2dp::{codec::MediaCodecConfig, media_types::*},
    bt_a2dp_sink_metrics as metrics,
    bt_avdtp::{self as avdtp, AvdtpControllerPool},
    fidl::encoding::Decodable,
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_bluetooth_component::LifecycleState,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        inspect::DebugExt,
        profile::find_profile_descriptors,
        types::{PeerId, Uuid},
    },
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::nodes::ManagedNode,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, FutureExt, StreamExt, TryStreamExt,
    },
    parking_lot::Mutex,
    std::{collections::hash_map, collections::HashMap, convert::TryFrom, sync::Arc},
};

use crate::connected_peers::ConnectedPeers;
use crate::inspect_types::StreamingInspectData;

mod avrcp_relay;
mod connected_peers;
mod inspect_types;
mod latm;
mod peer;
mod player;

/// Make the SDP definition for the A2DP sink service.
fn make_profile_service_definition() -> ServiceDefinition {
    ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(0x110B).into()]), // Audio Sink UUID
        protocol_descriptor_list: Some(vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement::Uint16(PSM_AVDTP)],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avdtp,
                params: vec![DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ]),
        profile_descriptors: Some(vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }]),
        ..ServiceDefinition::new_empty()
    }
}

// SDP Attribute ID for the Supported Features of A2DP.
// Defined in Assigned Numbers for SDP
// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_A2DP_SUPPORTED_FEATURES: u16 = 0x0311;

// Arbitrarily chosen ID for the SBC stream endpoint.
const SBC_SEID: u8 = 6;

// Arbitrarily chosen ID for the AAC stream endpoint.
const AAC_SEID: u8 = 7;

pub const DEFAULT_SAMPLE_RATE: u32 = 48000;
const DEFAULT_SESSION_ID: u64 = 0;

// Duration for A2DP-SNK to wait before assuming role of the initiator.
// If an L2CAP signaling channel has not been established by this time, A2DP-Sink will
// create the signaling channel, configure, open and start the stream.
const INITIATOR_DELAY: zx::Duration = zx::Duration::from_seconds(1);

/// Controls a stream endpoint and the media decoding task which is associated with it.
#[derive(Debug)]
struct Stream {
    /// The AVDTP endpoint that this stream is associated with.
    endpoint: avdtp::StreamEndpoint,
    /// Some(sender) when a stream task is started.  Signaling on this sender will
    /// end the media streaming task.
    suspend_sender: Option<Sender<()>>,
}

impl Stream {
    fn new(endpoint: avdtp::StreamEndpoint) -> Stream {
        Stream { endpoint, suspend_sender: None }
    }

    /// Get the currently configured extra codec data
    fn configuration(&self) -> Result<MediaCodecConfig, avdtp::Error> {
        self.endpoint
            .get_configuration()?
            .iter()
            .find_map(|cap| MediaCodecConfig::try_from(cap).ok())
            .ok_or(avdtp::Error::InvalidState)
    }

    /// Attempt to start the media decoding task.
    fn start(
        &mut self,
        inspect: StreamingInspectData,
        cobalt_sender: CobaltSender,
    ) -> avdtp::Result<()> {
        let start_res = self.endpoint.start();
        if start_res.is_err() || self.suspend_sender.is_some() {
            fx_log_info!("Start when streaming: {:?} {:?}", start_res, self.suspend_sender);
            return Err(avdtp::Error::InvalidState);
        }
        let (send, receive) = mpsc::channel(1);
        self.suspend_sender = Some(send);

        let codec_config = self.configuration()?;

        fuchsia_async::spawn_local(decode_media_stream(
            self.endpoint.take_transport()?,
            codec_config,
            // TODO(42976) get real media session id
            DEFAULT_SESSION_ID,
            receive,
            inspect,
            cobalt_sender,
        ));
        Ok(())
    }

    /// The encoding that media sent to this endpoint should be encoded with.
    /// This should be an encoding constant from fuchsia.media like AUDIO_ENCODING_SBC.
    /// See //sdk/fidl/fuchsia.media/stream_type.fidl for valid encodings.
    fn encoding(&self) -> avdtp::Result<&str> {
        let config = self.configuration()?;
        Ok(config.stream_encoding().clone())
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
        Stream { endpoint: self.endpoint.as_new(), suspend_sender: None }
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
        let sbc_config = MediaCodecConfig::min_sbc();
        if let Err(e) = player::Player::new(DEFAULT_SESSION_ID, sbc_config).await {
            fx_log_warn!("Can't play required SBC audio: {}", e);
            return Err(e);
        }
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )?;

        fx_vlog!(1, "Supported codec parameters: {:?}.", sbc_codec_info);

        let sbc_stream = avdtp::StreamEndpoint::new(
            SBC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                },
            ],
        )?;
        s.insert(sbc_stream);

        let aac_codec_info = AacCodecInfo::new(
            AacObjectType::MANDATORY_SNK,
            AacSamplingFrequency::MANDATORY_SNK,
            AacChannels::MANDATORY_SNK,
            true,
            0, // 0 = Unknown constant bitrate support (A2DP Sec. 4.5.2.4)
        )?;

        fx_vlog!(1, "Supported codec parameters: {:?}.", aac_codec_info);

        let aac_stream = avdtp::StreamEndpoint::new(
            AAC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: aac_codec_info.to_bytes().to_vec(),
                },
            ],
        )?;
        s.insert(aac_stream);

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
            writer.create_string("encoding", stream.encoding().unwrap_or("Unknown"));
            writer.create_string("capabilities", capabilities.debug());
        }
    }

    /// Adds a stream, indexing it by the endpoint id, associated with an encoding,
    /// replacing any other stream with the same endpoint id.
    fn insert(&mut self, stream: avdtp::StreamEndpoint) {
        self.0.insert(stream.local_id().clone(), Stream::new(stream));
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

    fn iter_mut(&mut self) -> hash_map::IterMut<'_, avdtp::StreamEndpointId, Stream> {
        self.0.iter_mut()
    }
}

/// Decodes a media stream by starting a Player and transferring media stream packets from AVDTP
/// to the player.  Restarts the player on player errors.
/// Ends when signaled from `end_signal`, or when the media transport stream is closed.
async fn decode_media_stream(
    mut stream: avdtp::MediaStream,
    codec_config: MediaCodecConfig,
    session_id: u64,
    mut end_signal: Receiver<()>,
    mut inspect: StreamingInspectData,
    cobalt_sender: CobaltSender,
) -> () {
    let mut player = match player::Player::new(session_id, codec_config.clone()).await {
        Ok(v) => v,
        Err(e) => {
            fx_log_info!("Can't setup player for Media: {:?}", e);
            return;
        }
    };

    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    inspect.stream_started();
    loop {
        select! {
            stream_packet = stream.next().fuse() => {
                let pkt = match stream_packet {
                    None => {
                        fx_log_info!("Media transport closed");
                        break;
                    },
                    Some(Err(e)) => {
                        fx_log_info!("Error in media stream: {:?}", e);
                        break;
                    }
                    Some(Ok(packet)) => packet,
                };

                if let Err(e) = player.push_payload(&pkt.as_slice()).await {
                    fx_log_info!("can't push packet: {:?}", e);
                }

                if !player.playing() {
                    player.play().unwrap_or_else(|e| fx_log_info!("Problem playing: {:}", e));
                }
                inspect.accumulated_bytes += pkt.len() as u64;
            },
            player_event = player.next_event().fuse() => {
                match player_event {
                    player::PlayerEvent::Closed => {
                        fx_log_info!("Rebuilding Player");
                        // The player died somehow? Attempt to rebuild the player.
                        player = match player::Player::new(session_id, codec_config.clone()).await {
                            Ok(v) => v,
                            Err(e) => {
                                fx_log_info!("Can't rebuild player: {:?}", e);
                                break;
                            }
                        };
                    },
                    player::PlayerEvent::Status(s) => {
                        fx_vlog!(1, "PlayerEvent Status happened: {:?}", s);
                    },
                }
            },
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

    report_stream_metrics(
        cobalt_sender,
        &codec_config.codec_type(),
        (end_time - start_time).into_seconds(),
    );
}

fn report_stream_metrics(
    mut cobalt_sender: CobaltSender,
    codec_type: &avdtp::MediaCodecType,
    duration_seconds: i64,
) {
    let codec = match codec_type {
        &avdtp::MediaCodecType::AUDIO_SBC => {
            metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Sbc
        }
        &avdtp::MediaCodecType::AUDIO_AAC => {
            metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Aac
        }
        _ => metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Unknown,
    };

    cobalt_sender.log_elapsed_time(
        metrics::A2DP_STREAM_DURATION_IN_SECONDS_METRIC_ID,
        codec as u32,
        duration_seconds,
    );
}

/// Establishes the signaling channel after the delay specified by `timer_expired`.
async fn connect_after_timeout(
    peer_id: PeerId,
    peers: Arc<Mutex<ConnectedPeers>>,
    controller_pool: Arc<Mutex<AvdtpControllerPool>>,
    profile_svc: ProfileProxy,
) {
    fx_vlog!(
        tag: "a2dp-sink",
        1,
        "A2DP sink - waiting {:?} before assuming INT role for peer {}.",
        INITIATOR_DELAY,
        peer_id,
    );
    fuchsia_async::Timer::new(INITIATOR_DELAY.after_now()).await;
    if peers.lock().is_connected(&peer_id) {
        fx_vlog!(
            tag: "a2dp-sink",
            1,
            "Peer {} has already connected. A2DP sink will not assume the INT role.",
            peer_id
        );
        return;
    }

    fx_vlog!(tag: "a2dp-sink", 1, "Remote peer has not established connection. A2DP sink will now assume the INT role.");
    let channel = match profile_svc
        .connect(&mut peer_id.into(), PSM_AVDTP, ChannelParameters::new_empty())
        .await
    {
        Err(e) => {
            fx_log_warn!("FIDL error creating channel: {:?}", e);
            return;
        }
        Ok(Err(e)) => {
            fx_log_warn!("Couldn't connect to {}: {:?}", peer_id, e);
            return;
        }
        Ok(Ok(channel)) => channel,
    };

    handle_connection(&peer_id, channel, true, &mut peers.lock(), &mut controller_pool.lock());
}

/// Handles incoming peer connections
fn handle_connection(
    peer_id: &PeerId,
    channel: Channel,
    initiate: bool,
    peers: &mut ConnectedPeers,
    controller_pool: &mut AvdtpControllerPool,
) {
    fx_log_info!("Connection from {}: {:?}!", peer_id, channel);
    peers.connected(peer_id.clone(), channel, initiate);
    if let Some(peer) = peers.get(&peer_id) {
        // Add the controller to the peers
        controller_pool.peer_connected(peer_id.clone(), peer.read().avdtp_peer());
    }
}

/// Handles found services. Stores the found information and then spawns a task which will
/// assume initator role after a delay.
fn handle_services_found(
    peer_id: &PeerId,
    attributes: &[Attribute],
    peers: Arc<Mutex<ConnectedPeers>>,
    controller_pool: Arc<Mutex<AvdtpControllerPool>>,
    profile_svc: ProfileProxy,
) {
    fx_log_info!("Audio Source found on {}, attributes: {:?}", peer_id, attributes);

    let profile = match find_profile_descriptors(attributes) {
        Ok(profiles) => profiles.into_iter().next().expect("at least one profile descriptor"),
        Err(_) => {
            fx_log_info!("Couldn't find profile in peer {} search results, ignoring.", peer_id);
            return;
        }
    };

    peers.lock().found(peer_id.clone(), profile);

    if peers.lock().is_connected(&peer_id) {
        return;
    }

    fasync::spawn(connect_after_timeout(
        peer_id.clone(),
        peers.clone(),
        controller_pool.clone(),
        profile_svc,
    ));
}

/// Options available from the command line
#[derive(FromArgs)]
#[argh(description = "Bluetooth Advanced Audio Distribution Profile: Sink")]
struct Opt {
    #[argh(option)]
    /// published Media Session Domain (optional, defaults to a native Fuchsia session)
    // TODO - Point to any media documentation about domains
    domain: Option<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts: Opt = argh::from_env();

    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");

    let controller_pool = Arc::new(Mutex::new(AvdtpControllerPool::new()));

    let inspect = inspect::Inspector::new();
    let mut fs = ServiceFs::new();
    inspect.serve(&mut fs)?;

    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.lock().connected(s));

    let mut lifecycle = ComponentLifecycleServer::spawn();
    fs.dir("svc").add_fidl_service(lifecycle.fidl_service());

    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_warn!("Unable to serve Inspect service directory: {}", e);
    }
    fasync::spawn(fs.collect::<()>());

    let cobalt_logger: CobaltSender = {
        let (sender, reporter) =
            CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));
        fasync::spawn(reporter);
        sender
    };

    let mut stream_inspect =
        ManagedNode::new(inspect.root().create_child("local stream endpoints"));
    let streams = Streams::build(&mut stream_inspect).await?;

    if streams.len() == 0 {
        return Err(format_err!("Can't play media - no codecs found or media player missing"));
    }

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;

    let peers = Arc::new(Mutex::new(connected_peers::ConnectedPeers::new(
        streams,
        profile_svc.clone(),
        cobalt_logger.clone(),
        inspect.root().create_child("connected"),
        opts.domain,
    )));

    let service_defs = vec![make_profile_service_definition()];

    let (connect_client, mut connect_requests) =
        create_request_stream().context("ConnectionReceiver creation")?;

    profile_svc.advertise(
        &mut service_defs.into_iter(),
        SecurityRequirements::new_empty(),
        ChannelParameters::new_empty(),
        connect_client,
    )?;

    const ATTRS: [u16; 4] = [
        ATTR_PROTOCOL_DESCRIPTOR_LIST,
        ATTR_SERVICE_CLASS_ID_LIST,
        ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];

    let (results_client, mut results_requests) =
        create_request_stream().context("SearchResults creation")?;

    profile_svc.search(ServiceClassProfileIdentifier::AudioSource, &ATTRS, results_client)?;

    lifecycle.set(LifecycleState::Ready).await.expect("lifecycle server to set value");

    loop {
        select! {
            connect_request = connect_requests.try_next() => {
                let connected = match connect_request? {
                    None => return Err(format_err!("BR/EDR ended service registration")),
                    Some(request) => request,
                };
                let ConnectionReceiverRequest::Connected { peer_id, channel, .. } = connected;
                handle_connection(&peer_id.into(), channel, false, &mut peers.lock(), &mut controller_pool.lock());
            }
            results_request = results_requests.try_next() => {
                let result = match results_request? {
                    None => return Err(format_err!("BR/EDR ended service search")),
                    Some(request) => request,
                };
                let SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } = result;

                handle_services_found(&peer_id.into(), &attributes, peers.clone(), controller_pool.clone(), profile_svc.clone());
            }
            complete => break,
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::Channel;
    use fidl_fuchsia_cobalt::{CobaltEvent, EventPayload};
    use fidl_fuchsia_media::{AUDIO_ENCODING_AAC, AUDIO_ENCODING_SBC};
    use fuchsia_bluetooth::types::PeerId;
    use futures::channel::mpsc;
    use futures::{task::Poll, StreamExt};
    use matches::assert_matches;

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn run_to_stalled(exec: &mut fasync::Executor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    fn setup_connected_peer_test() -> (
        fasync::Executor,
        PeerId,
        Arc<Mutex<ConnectedPeers>>,
        ProfileProxy,
        ProfileRequestStream,
        Arc<Mutex<AvdtpControllerPool>>,
    ) {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (proxy, stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();
        let id = PeerId(1);
        let inspect = inspect::Inspector::new();
        let peers = Arc::new(Mutex::new(ConnectedPeers::new(
            Streams::new(),
            proxy.clone(),
            cobalt_sender,
            inspect.root().create_child("connected"),
            None,
        )));

        let controller_pool = Arc::new(Mutex::new(AvdtpControllerPool::new()));

        (exec, id, peers, proxy, stream, controller_pool)
    }

    #[test]
    /// Test that the Streams specialized hashmap works as expected, storing
    /// the stream based on the SEID and retrieving the right pieces from
    /// the accessors.
    fn test_streams() {
        let mut streams = Streams::new();
        const LOCAL_ID: u8 = 1;
        const TEST_SAMPLE_FREQ: u32 = 44100;

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

        assert_matches!(streams.get_endpoint(&id), None);

        let res = streams.get_mut(&id);

        assert_matches!(res, Err(avdtp::ErrorCode::BadAcpSeid));

        streams.insert(s);

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

        let stream = streams.get_mut(&id).expect("stream");
        let codec_config = stream.configuration().expect("media codec config");
        assert_matches!(codec_config.sampling_frequency(), Ok(TEST_SAMPLE_FREQ));

        assert_matches!(stream.suspend_sender, None);
        assert_eq!(encoding, stream.encoding().expect("encoding"));
    }

    #[test]
    /// Test that a AAC endpoint stream works as expected to retrieve codec info
    fn test_aac_stream() {
        let mut streams = Streams::new();
        const LOCAL_ID: u8 = 1;
        const TEST_SAMPLE_FREQ: u32 = 44100;

        // An endpoint for testing
        let s = avdtp::StreamEndpoint::new(
            LOCAL_ID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: vec![128, 1, 4, 4, 226, 0],
                },
            ],
        )
        .expect("Failed to create endpoint");

        let id = s.local_id().clone();
        let information = s.information();
        let encoding = AUDIO_ENCODING_AAC.to_string();

        assert_matches!(streams.get_endpoint(&id), None);

        let res = streams.get_mut(&id);
        assert_matches!(res, Err(avdtp::ErrorCode::BadAcpSeid));

        streams.insert(s);

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
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        codec_extra: vec![128, 1, 4, 4, 226, 0],
                    },
                ],
            )
            .expect("Failed to configure endpoint");

        let stream = streams.get_mut(&id).expect("stream");
        let codec_extra = stream.configuration().expect("media codec config");
        assert_matches!(codec_extra.sampling_frequency(), Ok(TEST_SAMPLE_FREQ));

        assert_matches!(stream.suspend_sender, None);
        assert_eq!(encoding, stream.encoding().expect("encoding"));
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
    /// Test that cobalt metrics are sent after stream ends
    fn test_cobalt_metrics() {
        let (send, mut recv) = fake_cobalt_sender();
        const TEST_DURATION: i64 = 1;

        report_stream_metrics(send, &avdtp::MediaCodecType::AUDIO_AAC, TEST_DURATION);

        let event = recv.try_next().expect("no stream error").expect("event present");

        assert_eq!(
            event,
            CobaltEvent {
                metric_id: metrics::A2DP_STREAM_DURATION_IN_SECONDS_METRIC_ID,
                event_codes: vec![
                    metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Aac as u32
                ],
                component: None,
                payload: EventPayload::ElapsedMicros(TEST_DURATION),
            }
        );
    }

    #[test]
    /// Tests that A2DP sink assumes the initiator role when a peer is found, but
    /// not connected, and the timeout completes.
    fn wait_to_initiate_success_with_no_connected_peer() {
        let (mut exec, id, peers, proxy, mut prof_stream, controller_pool) =
            setup_connected_peer_test();
        // Initialize context to a fixed point in time.
        exec.set_fake_time(fasync::Time::from_nanos(1000000000));

        // Simulate getting the service found event.
        let attributes = vec![Attribute {
            id: ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: DataElement::Sequence(vec![Some(Box::new(DataElement::Sequence(vec![
                Some(Box::new(Uuid::from(ServiceClassProfileIdentifier::AudioSource).into())),
                Some(Box::new(DataElement::Uint16(0x0103))), // Version 1.3
            ])))]),
        }];
        handle_services_found(
            &id,
            &attributes,
            peers.clone(),
            controller_pool.clone(),
            proxy.clone(),
        );

        run_to_stalled(&mut exec);

        // At this point, a remote peer was found, but hasn't connected yet. There
        // should be no entry for it.
        assert!(!peers.lock().is_connected(&id));

        // Fast forward time by 5 seconds. In this time, the remote peer has not
        // connected.
        exec.set_fake_time(fasync::Time::from_nanos(6000000000));
        exec.wake_expired_timers();
        run_to_stalled(&mut exec);

        // After fast forwarding time, expect and handle the `connect` request
        // because A2DP-sink should be initiating.
        let (_test, transport) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let request = exec.run_until_stalled(&mut prof_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.into());
                responder
                    .send(&mut Ok(Channel { socket: Some(transport), ..Channel::new_empty() }))
                    .expect("responder sends");
            }
            x => panic!("Should have sent a connect request, but got {:?}", x),
        };
        run_to_stalled(&mut exec);

        // The remote peer did not connect to us, A2DP Sink should initiate a connection
        // and insert into `peers`.
        assert!(peers.lock().is_connected(&id));
    }

    #[test]
    /// Tests that A2DP sink does not assume the initiator role when a peer connects
    /// before `INITIATOR_DELAY` timeout completes.
    fn wait_to_initiate_returns_early_with_connected_peer() {
        let (mut exec, id, peers, proxy, mut prof_stream, controller_pool) =
            setup_connected_peer_test();
        // Initialize context to a fixed point in time.
        exec.set_fake_time(fasync::Time::from_nanos(1000000000));

        // Simulate getting the service found event.
        let attributes = vec![Attribute {
            id: ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: DataElement::Sequence(vec![Some(Box::new(DataElement::Sequence(vec![
                Some(Box::new(Uuid::from(ServiceClassProfileIdentifier::AudioSource).into())),
                Some(Box::new(DataElement::Uint16(0x0103))), // Version 1.3
            ])))]),
        }];
        handle_services_found(
            &id,
            &attributes,
            peers.clone(),
            controller_pool.clone(),
            proxy.clone(),
        );

        // At this point, a remote peer was found, but hasn't connected yet. There
        // should be no entry for it.
        assert!(!peers.lock().is_connected(&id));

        // Fast forward time by .5 seconds. The threshold is 1 second, so the timer
        // to initiate connections has not been triggered.
        exec.set_fake_time(fasync::Time::from_nanos(1500000000));
        exec.wake_expired_timers();
        run_to_stalled(&mut exec);

        // A peer connects before the timeout.
        let (_remote, transport) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let channel = Channel { socket: Some(transport), ..Channel::new_empty() };
        handle_connection(&id, channel, false, &mut peers.lock(), &mut controller_pool.lock());

        run_to_stalled(&mut exec);

        // The remote peer connected to us, and should be in the map.
        assert!(peers.lock().is_connected(&id));

        // Fast forward time by 4.5 seconds. Ensure no outbound connection is initiated
        // by us, since the remote peer has assumed the INT role.
        exec.set_fake_time(fasync::Time::from_nanos(6000000000));
        exec.wake_expired_timers();
        run_to_stalled(&mut exec);

        let request = exec.run_until_stalled(&mut prof_stream.next());
        match request {
            Poll::Ready(x) => panic!("There should be no l2cap connection requests: {:?}", x),
            Poll::Pending => {}
        };
        run_to_stalled(&mut exec);
    }
}
