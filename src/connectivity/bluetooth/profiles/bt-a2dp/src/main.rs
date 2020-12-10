// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    bt_a2dp::{
        codec::{CodecNegotiation, MediaCodecConfig},
        connected_peers::ConnectedPeers,
        media_types::*,
        peer::ControllerPool,
        stream,
    },
    bt_a2dp_metrics as metrics,
    bt_avdtp::{self as avdtp, ServiceCapability, ServiceCategory, StreamEndpoint},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_bluetooth_a2dp::{AudioModeRequest, AudioModeRequestStream, Role},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fidl_fuchsia_media_sessions2 as sessions2,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        profile::find_profile_descriptors,
        types::{Channel, PeerId, Uuid},
    },
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::Inspect,
    fuchsia_zircon as zx,
    futures::{self, select, StreamExt, TryStreamExt},
    log::{error, info, trace, warn},
    parking_lot::Mutex,
    std::{convert::TryFrom, convert::TryInto, sync::Arc},
};

mod avrcp_relay;
mod encoding;
mod latm;
mod pcm_audio;
mod player;
mod sink_task;
mod source_task;
mod sources;
mod volume_relay;

use crate::encoding::EncodedStream;
use crate::pcm_audio::PcmAudio;
use sources::AudioSourceType;

/// Make the SDP definition for the A2DP service.
fn make_profile_service_definition(service_uuid: Uuid) -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![service_uuid.into()]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![bredr::DataElement::Uint16(bredr::PSM_AVDTP)],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Avdtp,
                params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

// SDP Attribute ID for the Supported Features of A2DP.
// Defined in Assigned Numbers for SDP
// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_A2DP_SUPPORTED_FEATURES: u16 = 0x0311;

// Arbitrarily chosen IDs for the endpoints we might enable.
const SBC_SINK_SEID: u8 = 6;
const AAC_SINK_SEID: u8 = 7;
const SBC_SOURCE_SEID: u8 = 8;
const AAC_SOURCE_SEID: u8 = 9;

pub const DEFAULT_SAMPLE_RATE: u32 = 48000;
pub const DEFAULT_SESSION_ID: u64 = 0;

// Highest AAC bitrate we want to transmit
const MAX_BITRATE_AAC: u32 = 250000;

/// Pick a reasonable quality bitrate to use by default. 64k average per channel.
const PREFERRED_BITRATE_AAC: u32 = 128000;

// Duration for A2DP to wait before assuming role of the initiator.
// If a signaling channel has not been established by this time, A2DP will
// create the signaling channel, configure, open and start the stream.
const INITIATOR_DELAY: zx::Duration = zx::Duration::from_seconds(2);

fn find_codec_cap<'a>(endpoint: &'a StreamEndpoint) -> Option<&'a ServiceCapability> {
    endpoint.capabilities().iter().find(|cap| cap.category() == ServiceCategory::MediaCodec)
}

#[derive(Clone)]
struct StreamsBuilder {
    cobalt_sender: CobaltSender,
    codec_negotiation: CodecNegotiation,
    domain: Option<String>,
    aac_available: bool,
    source_type: AudioSourceType,
}

impl StreamsBuilder {
    async fn system_available(
        cobalt_sender: CobaltSender,
        domain: Option<String>,
        source_type: AudioSourceType,
    ) -> Result<Self, Error> {
        // TODO(fxbug.dev/1126): detect codecs, add streams for each codec
        // Sink codecs
        // SBC is required for sink.
        let sbc_endpoint = Self::build_sbc_sink_endpoint()?;
        let sbc_codec_cap = find_codec_cap(&sbc_endpoint).expect("just built");
        let sbc_config = MediaCodecConfig::try_from(sbc_codec_cap)?;
        if let Err(e) = player::Player::test_playable(&sbc_config).await {
            warn!("Can't play required SBC audio: {}", e);
            return Err(e);
        }

        let mut caps_available = vec![sbc_codec_cap.clone()];

        let aac_endpoint = Self::build_aac_sink_endpoint()?;
        let aac_codec_cap = find_codec_cap(&aac_endpoint).expect("just built");
        let aac_config = MediaCodecConfig::try_from(aac_codec_cap)?;
        let aac_available = player::Player::test_playable(&aac_config).await.is_ok();

        if aac_available {
            caps_available = vec![
                Self::build_aac_capability(avdtp::EndpointType::Sink, PREFERRED_BITRATE_AAC)?,
                sbc_codec_cap.clone(),
            ];
        }

        let codec_negotiation = CodecNegotiation::build(caps_available, avdtp::EndpointType::Sink)?;

        Ok(Self { cobalt_sender, codec_negotiation, domain, aac_available, source_type })
    }

    fn build_sbc_sink_endpoint() -> avdtp::Result<avdtp::StreamEndpoint> {
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )?;
        trace!("Supported SBC codec parameters: {:?}.", sbc_codec_info);

        avdtp::StreamEndpoint::new(
            SBC_SINK_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                ServiceCapability::MediaTransport,
                ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                },
            ],
        )
    }

    fn build_aac_capability(
        endpoint_type: avdtp::EndpointType,
        bitrate: u32,
    ) -> avdtp::Result<avdtp::ServiceCapability> {
        let codec_info = match endpoint_type {
            avdtp::EndpointType::Sink => AacCodecInfo::new(
                AacObjectType::MANDATORY_SNK,
                AacSamplingFrequency::MANDATORY_SNK,
                AacChannels::MANDATORY_SNK,
                true,
                bitrate,
            )?,
            avdtp::EndpointType::Source => AacCodecInfo::new(
                AacObjectType::MANDATORY_SRC,
                AacSamplingFrequency::FREQ48000HZ,
                AacChannels::TWO,
                true,
                bitrate,
            )?,
        };
        trace!("Supported AAC codec parameters: {:?}.", codec_info);
        Ok(ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_AAC,
            codec_extra: codec_info.to_bytes().to_vec(),
        })
    }

    fn build_aac_sink_endpoint() -> avdtp::Result<avdtp::StreamEndpoint> {
        let endpoint_type = avdtp::EndpointType::Sink;
        // 0 = Unknown constant bitrate support (A2DP Sec. 4.5.2.4)
        let codec_cap = Self::build_aac_capability(endpoint_type, /* bitrate = */ 0)?;
        avdtp::StreamEndpoint::new(
            AAC_SINK_SEID,
            avdtp::MediaType::Audio,
            endpoint_type,
            vec![ServiceCapability::MediaTransport, codec_cap],
        )
    }

    fn build_sbc_source_endpoint() -> avdtp::Result<avdtp::StreamEndpoint> {
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )?;
        trace!("Supported SBC codec parameters: {:?}.", sbc_codec_info);

        let codec_cap = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: sbc_codec_info.to_bytes().to_vec(),
        };

        avdtp::StreamEndpoint::new(
            SBC_SOURCE_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Source,
            vec![ServiceCapability::MediaTransport, codec_cap],
        )
    }

    fn build_aac_source_endpoint() -> avdtp::Result<avdtp::StreamEndpoint> {
        let endpoint_type = avdtp::EndpointType::Source;
        let codec_cap = Self::build_aac_capability(endpoint_type, MAX_BITRATE_AAC)?;
        avdtp::StreamEndpoint::new(
            AAC_SOURCE_SEID,
            avdtp::MediaType::Audio,
            endpoint_type,
            vec![ServiceCapability::MediaTransport, codec_cap],
        )
    }

    fn streams(&self) -> Result<stream::Streams, Error> {
        let publisher =
            fuchsia_component::client::connect_to_service::<sessions2::PublisherMarker>()
                .context("Failed to connect to MediaSession interface")?;

        let domain = self.domain.clone().unwrap_or("Bluetooth".to_string());
        let sink_task_builder =
            sink_task::SinkTaskBuilder::new(self.cobalt_sender.clone(), publisher, domain);
        let source_task_builder = source_task::SourceTaskBuilder::new(self.source_type);

        let mut streams = stream::Streams::new();

        let sbc_sink_endpoint = Self::build_sbc_sink_endpoint()?;
        streams.insert(stream::Stream::build(sbc_sink_endpoint, sink_task_builder.clone()));

        let sbc_source_endpoint = Self::build_sbc_source_endpoint()?;
        streams.insert(stream::Stream::build(sbc_source_endpoint, source_task_builder.clone()));

        if self.aac_available {
            let aac_sink_endpoint = Self::build_aac_sink_endpoint()?;
            streams.insert(stream::Stream::build(aac_sink_endpoint, sink_task_builder.clone()));
            let aac_source_endpoint = Self::build_aac_source_endpoint()?;
            streams.insert(stream::Stream::build(aac_source_endpoint, source_task_builder.clone()));
        }

        Ok(streams)
    }

    fn negotiation(&self) -> CodecNegotiation {
        self.codec_negotiation.clone()
    }
}

/// Establishes the signaling channel after an INITIATOR_DELAY.
async fn connect_after_timeout(
    peer_id: PeerId,
    peers: Arc<Mutex<ConnectedPeers>>,
    profile_svc: bredr::ProfileProxy,
    channel_mode: bredr::ChannelMode,
) {
    trace!(
        "A2DP - waiting {}s before connecting to peer {}.",
        INITIATOR_DELAY.into_seconds(),
        peer_id
    );
    fuchsia_async::Timer::new(INITIATOR_DELAY.after_now()).await;
    if peers.lock().is_connected(&peer_id) {
        return;
    }

    trace!("Peer has not established connection. A2DP assuming the INT role.");
    let channel = match profile_svc
        .connect(
            &mut peer_id.into(),
            &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters {
                psm: Some(bredr::PSM_AVDTP),
                parameters: Some(bredr::ChannelParameters {
                    channel_mode: Some(channel_mode),
                    ..bredr::ChannelParameters::EMPTY
                }),
                ..bredr::L2capParameters::EMPTY
            }),
        )
        .await
    {
        Err(e) => {
            warn!("FIDL error creating channel: {:?}", e);
            return;
        }
        Ok(Err(e)) => {
            warn!("Couldn't connect to {}: {:?}", peer_id, e);
            return;
        }
        Ok(Ok(channel)) => channel,
    };

    let channel = match channel.try_into() {
        Err(e) => {
            warn!("Didn't get channel from peer {}: {}", peer_id, e);
            return;
        }
        Ok(chan) => chan,
    };

    handle_connection(&peer_id, channel, /* initiate = */ true, &mut peers.lock());
}

/// Handles incoming peer connections
fn handle_connection(
    peer_id: &PeerId,
    channel: Channel,
    initiate: bool,
    peers: &mut ConnectedPeers,
) {
    info!("Connection from {}: {:?}!", peer_id, channel);
    let _ = peers.connected(peer_id.clone(), channel, initiate);
}

/// Handles found services. Stores the found information and then spawns a task which will
/// assume initator role after a delay.
fn handle_services_found(
    peer_id: &PeerId,
    attributes: &[bredr::Attribute],
    peers: Arc<Mutex<ConnectedPeers>>,
    profile_svc: bredr::ProfileProxy,
    channel_mode: bredr::ChannelMode,
) {
    info!("Audio profile found on {}, attributes: {:?}", peer_id, attributes);

    let profile = match find_profile_descriptors(attributes) {
        Ok(profiles) => profiles.into_iter().next().expect("at least one profile descriptor"),
        Err(_) => {
            info!("Couldn't find profile in peer {} search results, ignoring.", peer_id);
            return;
        }
    };

    peers.lock().found(peer_id.clone(), profile);

    if peers.lock().is_connected(&peer_id) {
        return;
    }

    fasync::Task::local(connect_after_timeout(
        peer_id.clone(),
        peers.clone(),
        profile_svc,
        channel_mode,
    ))
    .detach();
}

/// Parses the ChannelMode from the String argument.
///
/// Returns an Error if the provided argument is an invalid string.
fn channel_mode_from_arg(channel_mode: Option<String>) -> Result<bredr::ChannelMode, Error> {
    match channel_mode {
        None => Ok(bredr::ChannelMode::Basic),
        Some(s) if s == "basic" => Ok(bredr::ChannelMode::Basic),
        Some(s) if s == "ertm" => Ok(bredr::ChannelMode::EnhancedRetransmission),
        Some(s) => return Err(format_err!("invalid channel mode: {}", s)),
    }
}

/// Options available from the command line
#[derive(FromArgs)]
#[argh(description = "Bluetooth Advanced Audio Distribution Profile")]
struct Opt {
    #[argh(option)]
    /// published Media Session Domain (optional, defaults to a native Fuchsia session)
    // TODO - Point to any media documentation about domains
    domain: Option<String>,

    #[argh(option, default = "AudioSourceType::AudioOut")]
    /// audio source. options: [audio_out, big_ben]. Defaults to 'audio_out'
    source: AudioSourceType,

    #[argh(option, short = 'c', long = "channelmode")]
    /// channel mode preferred for the AVDTP signaling channel (optional, defaults to "basic", values: "basic", "ertm").
    channel_mode: Option<String>,
}

async fn test_encode_sbc() -> Result<(), Error> {
    // all sinks must support these options
    let required_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 48000,
        channel_map: vec![AudioChannelId::Lf],
    };
    EncodedStream::test(required_format, &MediaCodecConfig::min_sbc()).await
}

/// Handles role change requests from serving AudioMode
fn handle_audio_mode_connection(
    peers: Arc<Mutex<ConnectedPeers>>,
    mut stream: AudioModeRequestStream,
) {
    fasync::Task::spawn(async move {
        info!("AudioMode Client connected");
        while let Some(request) = stream.next().await {
            match request {
                Err(e) => info!("AudioMode client connection error: {}", e),
                Ok(AudioModeRequest::SetRole { role, responder }) => {
                    // We want to be `role` so we prefer to start streams of the opposite direction.
                    let direction = match role {
                        Role::Source => avdtp::EndpointType::Sink,
                        Role::Sink => avdtp::EndpointType::Source,
                    };
                    info!("Setting AudioMode to {:?}", role);
                    peers.lock().set_preferred_direction(direction);
                    if let Err(e) = responder.send() {
                        warn!("Failed to respond to mode request: {}", e);
                    }
                }
            }
        }
    })
    .detach();
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts: Opt = argh::from_env();

    fuchsia_syslog::init_with_tags(&["a2dp"]).expect("Can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let signaling_channel_mode = channel_mode_from_arg(opts.channel_mode)?;
    // Check to see that we can encode SBC audio.
    // This is a requireement of A2DP 1.3: Section 4.2
    if let Err(e) = test_encode_sbc().await {
        error!("Can't encode SBC Audio: {:?}", e);
        return Ok(());
    }
    let controller_pool = Arc::new(ControllerPool::new());

    let mut fs = ServiceFs::new();

    let inspect = inspect::Inspector::new();
    inspect.serve(&mut fs)?;

    let abs_vol_relay = volume_relay::VolumeRelay::start();
    if let Err(e) = &abs_vol_relay {
        info!("Failed to start AbsoluteVolume Relay: {:?}", e);
    }

    let cobalt_logger: CobaltSender = {
        let (sender, reporter) =
            CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));
        fasync::Task::spawn(reporter).detach();
        sender
    };

    let stream_builder =
        StreamsBuilder::system_available(cobalt_logger.clone(), opts.domain, opts.source).await?;

    let profile_svc = fuchsia_component::client::connect_to_service::<bredr::ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;

    let mut peers = ConnectedPeers::new(
        stream_builder.streams()?,
        stream_builder.negotiation(),
        profile_svc.clone(),
        Some(cobalt_logger.clone()),
    );
    if let Err(e) = peers.iattach(&inspect.root(), "connected") {
        warn!("Failed to attach to inspect: {:?}", e);
    }

    let peers_connected_stream = peers.connected_stream();
    let _controller_pool_connected_task = fasync::Task::spawn({
        let pool = controller_pool.clone();
        peers_connected_stream.map(move |p| pool.peer_connected(p)).collect::<()>()
    });

    let peers = Arc::new(Mutex::new(peers));

    fs.dir("svc").add_fidl_service(move |s| controller_pool.connected(s));
    fs.dir("svc").add_fidl_service({
        let peers = peers.clone();
        move |s| handle_audio_mode_connection(peers.clone(), s)
    });

    if let Err(e) = fs.take_and_serve_directory_handle() {
        warn!("Unable to serve service directory: {}", e);
    }
    let _servicefs_task = fasync::Task::spawn(fs.collect::<()>());

    let source_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::AudioSource as u16);
    let sink_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::AudioSink as u16);

    let service_defs = vec![
        make_profile_service_definition(source_uuid),
        make_profile_service_definition(sink_uuid),
    ];

    let (connect_client, connect_requests) =
        create_request_stream().context("ConnectionReceiver creation")?;

    let _ = profile_svc
        .advertise(
            &mut service_defs.into_iter(),
            bredr::ChannelParameters {
                channel_mode: Some(signaling_channel_mode),
                ..bredr::ChannelParameters::EMPTY
            },
            connect_client,
        )
        .check()?;

    const ATTRS: [u16; 4] = [
        bredr::ATTR_PROTOCOL_DESCRIPTOR_LIST,
        bredr::ATTR_SERVICE_CLASS_ID_LIST,
        bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];

    let (results_client, source_results) =
        create_request_stream().context("make request stream")?;
    profile_svc.search(
        bredr::ServiceClassProfileIdentifier::AudioSource,
        &ATTRS,
        results_client,
    )?;

    let (results_client, sink_results) = create_request_stream().context("make request stream")?;
    profile_svc.search(bredr::ServiceClassProfileIdentifier::AudioSink, &ATTRS, results_client)?;

    handle_profile_events(
        peers,
        profile_svc,
        signaling_channel_mode,
        connect_requests,
        source_results,
        sink_results,
    )
    .await
}

async fn handle_profile_events(
    peers: Arc<Mutex<ConnectedPeers>>,
    profile_svc: bredr::ProfileProxy,
    channel_mode: bredr::ChannelMode,
    mut connect_requests: bredr::ConnectionReceiverRequestStream,
    mut source_results: bredr::SearchResultsRequestStream,
    mut sink_results: bredr::SearchResultsRequestStream,
) -> Result<(), Error> {
    loop {
        select! {
            connect_request = connect_requests.try_next() => {
                let connected = match connect_request? {
                    None => return Err(format_err!("BR/EDR ended service registration")),
                    Some(request) => request,
                };
                let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } = connected;
                handle_connection(
                    &peer_id.into(),
                    channel.try_into()?,
                    /* initiate = */ false,
                    &mut peers.lock());
            }
            source_result = source_results.try_next() => {
                let result = source_result?.ok_or(format_err!("BR/EDR ended source service search"))?;
                match result {
                    bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } => {
                        handle_services_found(
                            &peer_id.into(),
                            &attributes,
                            peers.clone(),
                            profile_svc.clone(),
                            channel_mode.clone());
                        responder.send()?;
                    }
                    x => info!("Unhandled Search Result: {:?}", x),
                };
            }
            sink_result = sink_results.try_next() => {
                let result = sink_result?.ok_or(format_err!("BR/EDR ended sink service search"))?;
                match result {
                    bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } => {
                        handle_services_found(
                            &peer_id.into(),
                            &attributes,
                            peers.clone(),
                            profile_svc.clone(),
                            channel_mode.clone());
                        responder.send()?;
                    }
                    x => info!("Unhandled Search Result: {:?}", x),
                };
            }
            complete => break,
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use async_utils::PollExt;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_a2dp as a2dp;
    use fidl_fuchsia_bluetooth_bredr::{
        ConnectionReceiverMarker, ProfileRequest, ProfileRequestStream, SearchResultsMarker,
    };
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fuchsia_bluetooth::types::PeerId;
    use futures::channel::mpsc;
    use futures::{pin_mut, task::Poll, StreamExt};
    use matches::assert_matches;

    pub(crate) fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn run_to_stalled(exec: &mut fasync::Executor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    fn setup_connected_peers(
    ) -> (Arc<Mutex<ConnectedPeers>>, bredr::ProfileProxy, ProfileRequestStream) {
        let (proxy, stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();
        let peers = Arc::new(Mutex::new(ConnectedPeers::new(
            stream::Streams::new(),
            CodecNegotiation::build(vec![], avdtp::EndpointType::Sink).unwrap(),
            proxy.clone(),
            Some(cobalt_sender),
        )));
        (peers, proxy, stream)
    }

    #[test]
    fn test_responds_to_search_results() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (peers, profile_proxy, _profile_stream) = setup_connected_peers();
        let (sink_results_proxy, sink_results_stream) =
            create_proxy_and_stream::<SearchResultsMarker>()
                .expect("SearchResults proxy should be created");
        let (source_results_proxy, source_results_stream) =
            create_proxy_and_stream::<SearchResultsMarker>()
                .expect("SearchResults proxy should be created");
        let (_connect_proxy, connect_stream) =
            create_proxy_and_stream::<ConnectionReceiverMarker>()
                .expect("ConnectionReceiver proxy should be created");

        let handler_fut = handle_profile_events(
            peers,
            profile_proxy,
            bredr::ChannelMode::Basic,
            connect_stream,
            source_results_stream,
            sink_results_stream,
        );
        pin_mut!(handler_fut);

        let res = exec.run_until_stalled(&mut handler_fut);
        assert!(res.is_pending());

        // Report a sink search result, which should be replied to.
        let mut attributes = vec![];
        let results_fut = sink_results_proxy.service_found(
            &mut PeerId(1).into(),
            None,
            &mut attributes.iter_mut(),
        );
        pin_mut!(results_fut);

        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());
        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the result, got {:?}", x),
        };

        // Report a source search result, which should be replied to.
        let mut attributes = vec![];
        let results_fut = source_results_proxy.service_found(
            &mut PeerId(2).into(),
            None,
            &mut attributes.iter_mut(),
        );
        pin_mut!(results_fut);

        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());
        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the result, got {:?}", x),
        };

        // Handler should finish when one of the results is disconnected.
        drop(sink_results_proxy);
        let res = exec.run_until_stalled(&mut handler_fut).expect("handler should have finished");
        // Ending service search is an error condition for the handler.
        assert_matches!(res, Err(_));
    }

    #[test]
    /// build_local_streams should fail because it can't start the SBC encoder, because
    /// MediaPlayer isn't available in the test environment.
    fn test_sbc_unavailable_error() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        let (sender, _) = fake_cobalt_sender();
        let mut streams_fut =
            Box::pin(StreamsBuilder::system_available(sender, None, AudioSourceType::BigBen));

        let streams = exec.run_singlethreaded(&mut streams_fut);

        assert!(streams.is_err(), "Stream building should fail when it can't reach MediaPlayer");
    }

    #[test]
    /// Tests that A2DP sink assumes the initiator role when a peer is found, but
    /// not connected, and the timeout completes.
    fn wait_to_initiate_success_with_no_connected_peer() {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (peers, proxy, mut prof_stream) = setup_connected_peers();
        // Initialize context to a fixed point in time.
        exec.set_fake_time(fasync::Time::from_nanos(1000000000));
        let peer_id = PeerId(1);

        // Simulate getting the service found event.
        let attributes = vec![bredr::Attribute {
            id: bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: bredr::DataElement::Sequence(vec![Some(Box::new(
                bredr::DataElement::Sequence(vec![
                    Some(Box::new(
                        Uuid::from(bredr::ServiceClassProfileIdentifier::AudioSource).into(),
                    )),
                    Some(Box::new(bredr::DataElement::Uint16(0x0103))), // Version 1.3
                ]),
            ))]),
        }];
        handle_services_found(
            &peer_id,
            &attributes,
            peers.clone(),
            proxy.clone(),
            bredr::ChannelMode::Basic,
        );

        run_to_stalled(&mut exec);

        // At this point, a remote peer was found, but hasn't connected yet. There
        // should be no entry for it.
        assert!(!peers.lock().is_connected(&peer_id));

        // Fast forward time by 5 seconds. In this time, the remote peer has not
        // connected.
        exec.set_fake_time(fasync::Time::from_nanos(6000000000));
        exec.wake_expired_timers();
        run_to_stalled(&mut exec);

        // After fast forwarding time, expect and handle the `connect` request
        // because A2DP-sink should be initiating.
        let (_test, transport) = Channel::create();
        let request = exec.run_until_stalled(&mut prof_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.into());
                let channel = transport.try_into().unwrap();
                responder.send(&mut Ok(channel)).expect("responder sends");
            }
            x => panic!("Should have sent a connect request, but got {:?}", x),
        };
        run_to_stalled(&mut exec);

        // The remote peer did not connect to us, A2DP Sink should initiate a connection
        // and insert into `peers`.
        assert!(peers.lock().is_connected(&peer_id));
    }

    #[test]
    /// Tests that A2DP sink does not assume the initiator role when a peer connects
    /// before `INITIATOR_DELAY` timeout completes.
    fn wait_to_initiate_returns_early_with_connected_peer() {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (peers, proxy, mut prof_stream) = setup_connected_peers();
        // Initialize context to a fixed point in time.
        exec.set_fake_time(fasync::Time::from_nanos(1000000000));
        let peer_id = PeerId(1);

        // Simulate getting the service found event.
        let attributes = vec![bredr::Attribute {
            id: bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
            element: bredr::DataElement::Sequence(vec![Some(Box::new(
                bredr::DataElement::Sequence(vec![
                    Some(Box::new(
                        Uuid::from(bredr::ServiceClassProfileIdentifier::AudioSource).into(),
                    )),
                    Some(Box::new(bredr::DataElement::Uint16(0x0103))), // Version 1.3
                ]),
            ))]),
        }];
        handle_services_found(
            &peer_id,
            &attributes,
            peers.clone(),
            proxy.clone(),
            bredr::ChannelMode::Basic,
        );

        // At this point, a remote peer was found, but hasn't connected yet. There
        // should be no entry for it.
        assert!(!peers.lock().is_connected(&peer_id));

        // Fast forward time by .5 seconds. The threshold is 1 second, so the timer
        // to initiate connections has not been triggered.
        exec.set_fake_time(fasync::Time::from_nanos(1500000000));
        exec.wake_expired_timers();
        run_to_stalled(&mut exec);

        // A peer connects before the timeout.
        let (_remote, transport) = Channel::create();
        {
            let mut peers_lock = peers.lock();
            handle_connection(&peer_id, transport, /* initiate = */ false, &mut peers_lock);
        }

        run_to_stalled(&mut exec);

        // The remote peer connected to us, and should be in the map.
        assert!(peers.lock().is_connected(&peer_id));

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

    #[cfg(not(feature = "test_encoding"))]
    #[test]
    fn test_encoding_fails_in_test_environment() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let result = exec.run_singlethreaded(test_encode_sbc());

        assert!(result.is_err());
    }

    #[test]
    fn test_channel_mode_from_arg() {
        let channel_string = None;
        assert_matches!(channel_mode_from_arg(channel_string), Ok(bredr::ChannelMode::Basic));

        let channel_string = Some("basic".to_string());
        assert_matches!(channel_mode_from_arg(channel_string), Ok(bredr::ChannelMode::Basic));

        let channel_string = Some("ertm".to_string());
        assert_matches!(
            channel_mode_from_arg(channel_string),
            Ok(bredr::ChannelMode::EnhancedRetransmission)
        );

        let channel_string = Some("foobar123".to_string());
        assert!(channel_mode_from_arg(channel_string).is_err());
    }

    #[test]
    fn test_audio_mode_connection() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (peers, _profile_proxy, _profile_stream) = setup_connected_peers();

        let (proxy, stream) = create_proxy_and_stream::<a2dp::AudioModeMarker>()
            .expect("AudioMode proxy should be created");

        handle_audio_mode_connection(peers.clone(), stream);

        exec.run_singlethreaded(proxy.set_role(a2dp::Role::Sink)).expect("set role response");

        assert_eq!(avdtp::EndpointType::Source, peers.lock().preferred_direction());

        exec.run_singlethreaded(proxy.set_role(a2dp::Role::Source)).expect("set role response");

        assert_eq!(avdtp::EndpointType::Sink, peers.lock().preferred_direction());
    }
}
