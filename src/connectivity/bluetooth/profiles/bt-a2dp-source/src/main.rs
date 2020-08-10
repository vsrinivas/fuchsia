// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    async_helpers::component_lifecycle::ComponentLifecycleServer,
    bt_a2dp::{codec::MediaCodecConfig, media_types::*, peer::ControllerPool, peer::Peer, stream},
    bt_avdtp::{self as avdtp, ServiceCapability, ServiceCategory},
    fidl::{encoding::Decodable, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_component::LifecycleState,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak, LazyEntry},
        profile::find_profile_descriptors,
        types::{Channel, PeerId, Uuid},
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{self, select, StreamExt, TryStreamExt},
    std::{
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
};

mod encoding;
mod pcm_audio;
mod source_task;
mod sources;

use crate::encoding::EncodedStream;
use crate::pcm_audio::PcmAudio;
use sources::AudioSourceType;

/// Make the SDP definition for the A2DP source service.
fn make_profile_service_definition() -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(0x110A).into()]), // Audio Source UUID
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
        ..Decodable::new_empty()
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

// Highest AAC bitrate we want to transmit
const MAX_BITRATE_AAC: u32 = 250000;

fn build_sbc_endpoint(endpoint_type: avdtp::EndpointType) -> avdtp::Result<avdtp::StreamEndpoint> {
    let sbc_codec_info = SbcCodecInfo::new(
        SbcSamplingFrequency::FREQ48000HZ,
        SbcChannelMode::JOINT_STEREO,
        SbcBlockCount::MANDATORY_SRC,
        SbcSubBands::MANDATORY_SRC,
        SbcAllocation::MANDATORY_SRC,
        SbcCodecInfo::BITPOOL_MIN,
        SbcCodecInfo::BITPOOL_MAX,
    )?;
    fx_vlog!(1, "Supported SBC codec parameters: {:?}.", sbc_codec_info);

    avdtp::StreamEndpoint::new(
        SBC_SEID,
        avdtp::MediaType::Audio,
        endpoint_type,
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

fn build_aac_endpoint(endpoint_type: avdtp::EndpointType) -> avdtp::Result<avdtp::StreamEndpoint> {
    let aac_codec_info = AacCodecInfo::new(
        AacObjectType::MANDATORY_SRC,
        AacSamplingFrequency::FREQ48000HZ,
        AacChannels::TWO,
        true,
        MAX_BITRATE_AAC,
    )?;

    fx_vlog!(1, "Supported AAC codec parameters: {:?}.", aac_codec_info);

    avdtp::StreamEndpoint::new(
        AAC_SEID,
        avdtp::MediaType::Audio,
        endpoint_type,
        vec![
            ServiceCapability::MediaTransport,
            ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                codec_extra: aac_codec_info.to_bytes().to_vec(),
            },
        ],
    )
}

/// Builds the set of streams which we currently support, streaming from the source_type given.
fn build_local_streams(source_type: AudioSourceType) -> avdtp::Result<stream::Streams> {
    // TODO(BT-533): detect codecs, add streams for each codec

    let source_task_builder = source_task::SourceTaskBuilder::new(source_type);
    let mut streams = stream::Streams::new();

    streams.insert(stream::Stream::build(
        build_sbc_endpoint(avdtp::EndpointType::Source)?,
        source_task_builder.clone(),
    ));
    fx_vlog!(1, "SBC Stream added at SEID {}", SBC_SEID);

    streams.insert(stream::Stream::build(
        build_aac_endpoint(avdtp::EndpointType::Source)?,
        source_task_builder,
    ));
    fx_vlog!(1, "AAC stream added at SEID {}", AAC_SEID);

    Ok(streams)
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

struct Peers {
    peers: DetachableMap<PeerId, Peer>,
    streams: stream::Streams,
    profile: bredr::ProfileProxy,
    /// The L2CAP channel mode preference for the AVDTP signaling channel.
    channel_mode: bredr::ChannelMode,
    /// The controller used for sending out-of-band commands.
    controller: Arc<ControllerPool>,
}

const INITIATOR_DELAY: zx::Duration = zx::Duration::from_seconds(2);

impl Peers {
    fn new(
        streams: stream::Streams,
        profile: bredr::ProfileProxy,
        channel_mode: bredr::ChannelMode,
        controller: Arc<ControllerPool>,
    ) -> Self {
        Peers { peers: DetachableMap::new(), profile, streams, channel_mode, controller }
    }

    #[cfg(test)]
    pub(crate) fn get(&self, id: &PeerId) -> Option<Arc<Peer>> {
        self.peers.get(id).and_then(|p| p.upgrade())
    }

    /// Handle te first connection to a Peer is established, either after discovery or on
    /// receiving a peer connection.  Uses the Channel to create the Peer object with the given
    /// streams, which handles requests and commands to the peer.
    fn add_control_connection(
        entry: LazyEntry<PeerId, Peer>,
        channel: Channel,
        desc: Option<bredr::ProfileDescriptor>,
        streams: stream::Streams,
        profile: bredr::ProfileProxy,
        controller_pool: Arc<ControllerPool>,
    ) -> Result<(), Error> {
        let id = entry.key();
        let avdtp_peer = avdtp::Peer::new(channel);
        let peer = Peer::create(id.clone(), avdtp_peer, streams, profile, None);
        // Start the streaming task if the profile information is populated.
        // Otherwise, `self.discovered()` will do so.
        let start_streaming_flag = desc.map_or(false, |d| {
            peer.set_descriptor(d);
            true
        });

        let closed_fut = peer.closed();
        let detached_peer = match entry.try_insert(peer) {
            Ok(detached_peer) => detached_peer,
            Err(_peer) => {
                fx_log_info!("Peer connected to us, aborting.");
                return Err(format_err!("Couldn't start peer"));
            }
        };

        // Alert the ControllerPool of a new peer.
        controller_pool.peer_connected(id.clone(), detached_peer.clone());

        if start_streaming_flag {
            Peers::spawn_streaming(entry.clone());
        }

        let peer_id = id.clone();
        fasync::Task::local(async move {
            closed_fut.await;
            fx_log_info!("Detaching closed peer {}", peer_id);
            detached_peer.detach();
        })
        .detach();
        Ok(())
    }

    /// Called when a peer is discovered via SDP.
    fn discovered(&mut self, id: PeerId, desc: bredr::ProfileDescriptor) {
        let entry = self.peers.lazy_entry(&id);
        let profile = self.profile.clone();
        let streams = self.streams.as_new();
        let channel_mode = self.channel_mode.clone();
        let controller_pool = self.controller.clone();
        fasync::Task::local(async move {
            fx_log_info!("Waiting {:?} to connect to discovered peer {}", INITIATOR_DELAY, id);
            fasync::Timer::new(INITIATOR_DELAY.after_now()).await;
            if let Some(peer) = entry.get() {
                fx_log_info!("After initiator delay, {} was connected, not connecting..", id);
                if let Some(peer) = peer.upgrade() {
                    if peer.set_descriptor(desc.clone()).is_none() {
                        // TODO(50465): maybe check to see if we should start streaming.
                        Peers::spawn_streaming(entry.clone());
                    }
                }
                return;
            }
            let channel = match profile
                .connect(
                    &mut id.into(),
                    &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters {
                        psm: Some(bredr::PSM_AVDTP),
                        parameters: Some(bredr::ChannelParameters {
                            channel_mode: Some(channel_mode),
                            ..Decodable::new_empty()
                        }),
                        ..Decodable::new_empty()
                    }),
                )
                .await
            {
                Err(e) => {
                    fx_log_warn!("FIDL error connecting to peer {}: {:?}", id, e);
                    return;
                }
                Ok(Err(code)) => {
                    fx_log_info!("Couldn't connect to peer {}: {:?}", id, code);
                    return;
                }
                Ok(Ok(channel)) => channel,
            };
            let channel = match channel.try_into() {
                Ok(chan) => chan,
                Err(e) => {
                    fx_log_warn!("No channel from peer {}: {:?}", id, e);
                    return;
                }
            };
            if let Err(e) = Peers::add_control_connection(
                entry,
                channel,
                Some(desc),
                streams,
                profile,
                controller_pool,
            ) {
                fx_log_warn!("Error adding control connection for {}: {:?}", id, e);
            }
        })
        .detach();
    }

    /// Called when a peer initiates a connection. If it is the first active connection, it creates
    /// a new Peer to handle communication.
    fn connected(&mut self, id: PeerId, channel: Channel) -> Result<(), Error> {
        let entry = self.peers.lazy_entry(&id);
        if let Some(peer) = entry.get() {
            if let Some(peer) = peer.upgrade() {
                if let Err(e) = peer.receive_channel(channel) {
                    fx_log_warn!("{} connected an unexpected channel: {}", id, e);
                }
            }
            return Ok(());
        }
        Peers::add_control_connection(
            entry,
            channel,
            None,
            self.streams.as_new(),
            self.profile.clone(),
            self.controller.clone(),
        )
    }

    /// Attempt to start a media stream to `peer`
    fn spawn_streaming(entry: LazyEntry<PeerId, Peer>) {
        let weak_peer = match entry.get() {
            None => return,
            Some(peer) => peer,
        };
        fuchsia_async::Task::local(async move {
            if let Err(e) = start_streaming(&weak_peer).await {
                fx_log_info!("Failed to stream: {:?}", e);
                weak_peer.detach();
            }
        })
        .detach();
    }
}

/// Pick a reasonable quality bitrate to use by default. 64k average per channel.
const PREFERRED_BITRATE_AAC: u32 = 128000;

/// Represents a chosen remote stream endpoint and negotiated codec and encoder settings
#[derive(Debug)]
struct SelectedStream<'a> {
    remote_stream: &'a avdtp::StreamEndpoint,
    codec_settings: ServiceCapability,
    seid: u8,
}

impl<'a> SelectedStream<'a> {
    /// From the list of available remote streams, pick our preferred one and return matching codec parameters.
    fn pick(
        remote_streams: &'a Vec<avdtp::StreamEndpoint>,
    ) -> Result<SelectedStream<'_>, anyhow::Error> {
        // prefer AAC
        let (remote_stream, seid) =
            match Self::find_stream(remote_streams, &avdtp::MediaCodecType::AUDIO_AAC) {
                Some(aac_stream) => (aac_stream, AAC_SEID),
                None => (
                    Self::find_stream(remote_streams, &avdtp::MediaCodecType::AUDIO_SBC)
                        .ok_or(format_err!("Couldn't find a compatible stream"))?,
                    SBC_SEID,
                ),
            };

        let codec_settings = Self::negotiate_codec_settings(remote_stream)?;

        Ok(SelectedStream { remote_stream, codec_settings, seid })
    }

    /// From `stream` remote options, select our preferred and supported encoding options
    fn negotiate_codec_settings(
        stream: &'a avdtp::StreamEndpoint,
    ) -> Result<ServiceCapability, Error> {
        let codec_cap = Self::get_codec_cap(stream).ok_or(format_err!("no codec extra found"))?;
        match codec_cap {
            ServiceCapability::MediaCodec {
                codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                codec_extra,
                ..
            } => {
                let remote_codec_info = AacCodecInfo::try_from(&codec_extra[..])?;
                let negotiated_bitrate =
                    std::cmp::min(remote_codec_info.bitrate(), PREFERRED_BITRATE_AAC);
                // Remote peers have to support these options
                let aac_codec_info = AacCodecInfo::new(
                    AacObjectType::MPEG2_AAC_LC,
                    AacSamplingFrequency::FREQ48000HZ,
                    AacChannels::TWO,
                    true,
                    negotiated_bitrate,
                )?;
                Ok(ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: aac_codec_info.to_bytes().to_vec(),
                })
            }
            ServiceCapability::MediaCodec {
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra,
                ..
            } => {
                // TODO(39321): Choose codec options based on availability and quality.
                let remote_codec_info = SbcCodecInfo::try_from(&codec_extra[..])?;
                // Attempt to use the recommended "High Quality" bitpool value.
                let mut bitpool_value = 51;
                bitpool_value = std::cmp::min(bitpool_value, remote_codec_info.max_bitpool());
                let sbc_codec_info = SbcCodecInfo::new(
                    SbcSamplingFrequency::FREQ48000HZ,
                    SbcChannelMode::JOINT_STEREO,
                    SbcBlockCount::SIXTEEN,
                    SbcSubBands::EIGHT,
                    SbcAllocation::LOUDNESS,
                    /* min_bpv= */ bitpool_value,
                    /* max_bpv= */ bitpool_value,
                )?;

                Ok(ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                })
            }
            ServiceCapability::MediaCodec { codec_type, .. } => {
                Err(format_err!("Unsupported codec {:?}", codec_type))
            }
            x => Err(format_err!("Expected MediaCodec and got {:?}", x)),
        }
    }

    fn find_stream(
        remote_streams: &'a Vec<avdtp::StreamEndpoint>,
        codec_type: &avdtp::MediaCodecType,
    ) -> Option<&'a avdtp::StreamEndpoint> {
        remote_streams
            .iter()
            .filter(|stream| stream.information().endpoint_type() == &avdtp::EndpointType::Sink)
            .find(|stream| stream.codec_type() == Some(codec_type))
    }

    fn get_codec_cap(stream: &'a avdtp::StreamEndpoint) -> Option<&'a ServiceCapability> {
        stream.capabilities().iter().find(|cap| cap.category() == ServiceCategory::MediaCodec)
    }
}

async fn start_streaming(peer: &DetachableWeak<PeerId, Peer>) -> Result<(), anyhow::Error> {
    let streams_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.collect_capabilities()
    };
    let remote_streams = streams_fut.await?;
    let selected_stream = SelectedStream::pick(&remote_streams)?;

    let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
    strong
        .stream_start(
            selected_stream.seid.try_into()?,
            selected_stream.remote_stream.local_id().clone(),
            selected_stream.codec_settings.clone(),
        )
        .await
        .map_err(Into::into)
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

/// Defines the options available from the command line
#[derive(FromArgs)]
#[argh(description = "Bluetooth Advanced Audio Distribution Profile: Source")]
struct Opt {
    #[argh(option, default = "AudioSourceType::AudioOut")]
    /// audio source. options: [audio_out, big_ben]. Defaults to 'audio_out'
    source: AudioSourceType,

    #[argh(option, short = 'c', long = "channelmode")]
    /// channel mode preferred for the AVDTP signaling channel (optional, defaults to "basic", values: "basic", "ertm").
    channel_mode: Option<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get the command line args.
    let opts: Opt = argh::from_env();

    fuchsia_syslog::init_with_tags(&["a2dp-source"]).expect("Can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let signaling_channel_mode = channel_mode_from_arg(opts.channel_mode)?;
    // Check to see that we can encode SBC audio.
    // This is a requireement of A2DP 1.3: Section 4.2
    if let Err(e) = test_encode_sbc().await {
        fx_log_err!("Can't encode SBC Audio: {:?}", e);
        return Ok(());
    }
    let controller_pool = Arc::new(ControllerPool::new());

    let mut fs = ServiceFs::new();

    let mut lifecycle = ComponentLifecycleServer::spawn();
    fs.dir("svc").add_fidl_service(lifecycle.fidl_service());

    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.connected(s));
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_warn!("Unable to serve Inspect service directory: {}", e);
    }

    fasync::Task::spawn(fs.collect::<()>()).detach();

    let profile_svc = fuchsia_component::client::connect_to_service::<bredr::ProfileMarker>()
        .context("connecting to Bluetooth profile service")?;

    let service_defs = vec![make_profile_service_definition()];

    let (connect_client, connect_requests) =
        create_request_stream().context("ConnectionReceiver creation")?;

    profile_svc
        .advertise(
            &mut service_defs.into_iter(),
            Decodable::new_empty(),
            bredr::ChannelParameters {
                channel_mode: Some(signaling_channel_mode),
                ..Decodable::new_empty()
            },
            connect_client,
        )
        .context("advertising A2DP service")?;

    const ATTRS: [u16; 4] = [
        bredr::ATTR_PROTOCOL_DESCRIPTOR_LIST,
        bredr::ATTR_SERVICE_CLASS_ID_LIST,
        bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];

    let (results_client, results_requests) =
        create_request_stream().context("SearchResults creation")?;

    profile_svc.search(bredr::ServiceClassProfileIdentifier::AudioSink, &ATTRS, results_client)?;

    let streams = build_local_streams(opts.source)?;
    let peers = Peers::new(streams, profile_svc, signaling_channel_mode, controller_pool);

    lifecycle.set(LifecycleState::Ready).await.expect("lifecycle server to set value");

    handle_profile_events(peers, connect_requests, results_requests).await
}

async fn handle_profile_events(
    mut peers: Peers,
    mut connect_requests: bredr::ConnectionReceiverRequestStream,
    mut results_requests: bredr::SearchResultsRequestStream,
) -> Result<(), Error> {
    loop {
        select! {
            connect_request = connect_requests.try_next() => {
                let connected = match connect_request? {
                    None => return Err(format_err!("BR/EDR ended service registration")),
                    Some(request) => request,
                };
                let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } = connected;
                let peer_id: PeerId = peer_id.into();
                fx_log_info!("Connected sink {}", peer_id);
                let channel = match channel.try_into() {
                    Ok(chan) => chan,
                    Err(e) => {
                        fx_log_info!("Peer {}: channel is not usable: {:?}", peer_id, e);
                        continue;
                    }
                };
                if let Err(e) = peers.connected(peer_id, channel) {
                    fx_log_info!("Error connecting peer {}: {:?}", peer_id, e);
                    continue;
                }
            },
            results_request = results_requests.try_next() => {
                let result = match results_request? {
                    None => return Err(format_err!("BR/EDR ended service search")),
                    Some(request) => request,
                };
                let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } = result;
                let peer_id: PeerId = peer_id.into();
                fx_log_info!(
                    "Discovered sink {} - protocol {:?}: {:?}",
                    peer_id,
                    protocol,
                    attributes
                );
                responder.send()?;
                let profile = match find_profile_descriptors(&attributes) {
                    Ok(profiles) => profiles.into_iter().next().expect("at least one profile descriptor"),
                    Err(_) => {
                        fx_log_info!("Couldn't find profile in peer {} search results, ignoring.", peer_id);
                        continue;
                    }
                };
                peers.discovered(peer_id, profile);
            },
            complete => break,
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use futures::{pin_mut, task::Poll};
    use matches::assert_matches;

    #[test]
    fn test_responds_to_search_results() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (profile_proxy, _profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");
        let (results_proxy, results_stream) =
            create_proxy_and_stream::<bredr::SearchResultsMarker>()
                .expect("SearchResults proxy should be created");
        let (_connect_proxy, connect_stream) =
            create_proxy_and_stream::<bredr::ConnectionReceiverMarker>()
                .expect("ConnectionReceiver proxy should be created");
        let controller = Arc::new(ControllerPool::new());

        let peers = Peers::new(
            stream::Streams::new(),
            profile_proxy,
            bredr::ChannelMode::Basic,
            controller,
        );

        let handler_fut = handle_profile_events(peers, connect_stream, results_stream);
        pin_mut!(handler_fut);

        let res = exec.run_until_stalled(&mut handler_fut);
        assert!(res.is_pending());

        // Report a search result. This should be replied to.
        let mut attributes = vec![];
        let results_fut =
            results_proxy.service_found(&mut PeerId(1).into(), None, &mut attributes.iter_mut());
        pin_mut!(results_fut);

        let res = exec.run_until_stalled(&mut handler_fut);
        assert!(res.is_pending());
        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the result, got {:?}", x),
        };
    }

    #[test]
    fn test_encoding_fails_in_test_environment() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let result = exec.run_singlethreaded(test_encode_sbc());

        assert!(result.is_err());
    }

    fn setup_peers_test() -> (fasync::Executor, PeerId, Peers, bredr::ProfileRequestStream) {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (proxy, stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");
        let controller = Arc::new(ControllerPool::new());
        let id = PeerId(1);

        let peers =
            Peers::new(stream::Streams::new(), proxy, bredr::ChannelMode::Basic, controller);

        (exec, id, peers, stream)
    }

    fn run_to_stalled(exec: &mut fasync::Executor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    #[test]
    fn peers_peer_disconnect_removes_peer() {
        let (mut exec, id, mut peers, _stream) = setup_peers_test();

        let (remote, signaling) = Channel::create();

        let _ = peers.connected(id, signaling);
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());
    }

    #[test]
    fn peers_reconnect_works() {
        let (mut exec, id, mut peers, _stream) = setup_peers_test();

        let (remote, signaling) = Channel::create();
        let _ = peers.connected(id, signaling);

        assert!(peers.get(&id).is_some());

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());

        // Connect another peer with the same ID
        let (_remote, signaling) = Channel::create();
        if let Err(e) = peers.connected(id, signaling) {
            panic!("Expected connected to return Ok(()), got {:?}", e);
        }

        // Should be connected.
        assert!(peers.get(&id).is_some());
    }

    #[test]
    fn test_stream_selection() {
        let sbc_endpoint = build_sbc_endpoint(avdtp::EndpointType::Sink).expect("sbc endpoint");
        let aac_endpoint = build_aac_endpoint(avdtp::EndpointType::Sink).expect("aac endpoint");

        // test that aac is preferred
        let remote_streams = vec![aac_endpoint.as_new(), sbc_endpoint.as_new()];
        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: AAC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        ..
                    },
                ..
            })
        );

        // only sbc available, should return sbc
        let remote_streams = vec![sbc_endpoint.as_new()];
        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: SBC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        ..
                    },
                ..
            })
        );

        // none available, should error
        let remote_streams = vec![];
        assert!(SelectedStream::pick(&remote_streams).is_err());

        // When remote end has a bitpool max that is lower than our preferred, the selected bitpool
        // value doesn't go out of range.
        let lower_max_bpv = 45;
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            lower_max_bpv,
        )
        .expect("sbc_codec_info");
        let remote_streams = vec![avdtp::StreamEndpoint::new(
            SBC_SEID,
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
        .expect("stream endpoint")];

        match SelectedStream::pick(&remote_streams) {
            Ok(SelectedStream {
                seid: SBC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        codec_extra,
                    },
                ..
            }) => {
                let codec_info =
                    SbcCodecInfo::try_from(&codec_extra[..]).expect("codec extra to parse");
                assert!(codec_info.max_bitpool() <= lower_max_bpv);
            }
            x => panic!("Expected Ok Selected SBC stream, got {:?}", x),
        };
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
}
