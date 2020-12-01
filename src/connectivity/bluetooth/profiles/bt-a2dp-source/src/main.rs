// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    async_helpers::component_lifecycle::ComponentLifecycleServer,
    bt_a2dp::{
        codec::{CodecNegotiation, MediaCodecConfig},
        connected_peers::ConnectedPeers,
        media_types::*,
        peer::ControllerPool,
        stream,
    },
    bt_avdtp::{self as avdtp, ServiceCapability},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_component::LifecycleState,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        profile::find_profile_descriptors,
        types::{PeerId, Uuid},
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::Inspect,
    fuchsia_zircon as zx,
    futures::{self, lock::Mutex, select, StreamExt, TryStreamExt},
    log::{error, info, trace, warn},
    std::{convert::TryInto, sync::Arc},
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
        ..bredr::ServiceDefinition::EMPTY
    }
}

// SDP Attribute ID for the Supported Features of A2DP.
// Defined in Assigned Numbers for SDP
// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_A2DP_SUPPORTED_FEATURES: u16 = 0x0311;

// Arbitrarily chosen ID for the SBC stream endpoint.
pub const SBC_SEID: u8 = 6;

// Arbitrarily chosen ID for the AAC stream endpoint.
pub const AAC_SEID: u8 = 7;

// Highest AAC bitrate we want to transmit
const MAX_BITRATE_AAC: u32 = 250000;

/// Pick a reasonable quality bitrate to use by default. 64k average per channel.
const PREFERRED_BITRATE_AAC: u32 = 128000;

fn build_sbc_capability() -> avdtp::Result<avdtp::ServiceCapability> {
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
    Ok(ServiceCapability::MediaCodec {
        media_type: avdtp::MediaType::Audio,
        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
        codec_extra: sbc_codec_info.to_bytes().to_vec(),
    })
}

pub fn build_sbc_endpoint(
    endpoint_type: avdtp::EndpointType,
) -> avdtp::Result<avdtp::StreamEndpoint> {
    avdtp::StreamEndpoint::new(
        SBC_SEID,
        avdtp::MediaType::Audio,
        endpoint_type,
        vec![ServiceCapability::MediaTransport, build_sbc_capability()?],
    )
}

fn build_aac_capability(bitrate: u32) -> avdtp::Result<avdtp::ServiceCapability> {
    let aac_codec_info = AacCodecInfo::new(
        AacObjectType::MANDATORY_SRC,
        AacSamplingFrequency::FREQ48000HZ,
        AacChannels::TWO,
        true,
        bitrate,
    )?;
    trace!("Supported AAC codec parameters: {:?}.", aac_codec_info);

    Ok(ServiceCapability::MediaCodec {
        media_type: avdtp::MediaType::Audio,
        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
        codec_extra: aac_codec_info.to_bytes().to_vec(),
    })
}

pub fn build_aac_endpoint(
    endpoint_type: avdtp::EndpointType,
) -> avdtp::Result<avdtp::StreamEndpoint> {
    avdtp::StreamEndpoint::new(
        AAC_SEID,
        avdtp::MediaType::Audio,
        endpoint_type,
        vec![ServiceCapability::MediaTransport, build_aac_capability(MAX_BITRATE_AAC)?],
    )
}

/// Builds the set of streams which we currently support, streaming from the source_type given.
fn build_local_streams(source_type: AudioSourceType) -> avdtp::Result<stream::Streams> {
    // TODO(fxbug.dev/1126): detect codecs, add streams for each codec

    let source_task_builder = source_task::SourceTaskBuilder::new(source_type);
    let mut streams = stream::Streams::new();

    streams.insert(stream::Stream::build(
        build_sbc_endpoint(avdtp::EndpointType::Source)?,
        source_task_builder.clone(),
    ));
    trace!("SBC Stream added at SEID {}", SBC_SEID);

    streams.insert(stream::Stream::build(
        build_aac_endpoint(avdtp::EndpointType::Source)?,
        source_task_builder,
    ));
    trace!("AAC stream added at SEID {}", AAC_SEID);

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
        error!("Can't encode SBC Audio: {:?}", e);
        return Ok(());
    }
    let controller_pool = Arc::new(ControllerPool::new());

    let mut fs = ServiceFs::new();

    let inspect = inspect::Inspector::new();
    inspect.serve(&mut fs)?;

    let mut lifecycle = ComponentLifecycleServer::spawn();
    fs.dir("svc").add_fidl_service(lifecycle.fidl_service());

    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.connected(s));

    if let Err(e) = fs.take_and_serve_directory_handle() {
        warn!("Unable to serve service directory: {}", e);
    }
    let _servicefs_task = fasync::Task::spawn(fs.collect::<()>());

    let profile_svc = fuchsia_component::client::connect_to_service::<bredr::ProfileMarker>()
        .context("connecting to Bluetooth profile service")?;

    let service_defs = vec![make_profile_service_definition()];

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

    let (results_client, results_requests) =
        create_request_stream().context("SearchResults creation")?;

    profile_svc.search(bredr::ServiceClassProfileIdentifier::AudioSink, &ATTRS, results_client)?;

    let streams = build_local_streams(opts.source)?;
    let negotiation = CodecNegotiation::build(vec![
        build_aac_capability(PREFERRED_BITRATE_AAC)?,
        build_sbc_capability()?,
    ])?;

    let mut peers = ConnectedPeers::new(streams, negotiation, profile_svc.clone(), None);
    if let Err(e) = peers.iattach(inspect.root(), "connected") {
        info!("Failed to attach peers to inspect: {:?}", e);
    }

    let peers_connected_stream = peers.connected_stream();
    let _controller_pool_connected_task = fasync::Task::spawn(
        peers_connected_stream.map(move |p| controller_pool.peer_connected(p)).collect::<()>(),
    );

    lifecycle.set(LifecycleState::Ready).await.expect("lifecycle server to set value");

    handle_profile_events(
        peers,
        connect_requests,
        results_requests,
        profile_svc,
        signaling_channel_mode,
    )
    .await
}

async fn handle_profile_events(
    peers: ConnectedPeers,
    mut connect_requests: bredr::ConnectionReceiverRequestStream,
    mut results_requests: bredr::SearchResultsRequestStream,
    profile: bredr::ProfileProxy,
    signaling_channel_mode: bredr::ChannelMode,
) -> Result<(), Error> {
    let peers = Arc::new(Mutex::new(peers));
    loop {
        select! {
            connect_request = connect_requests.try_next() => {
                let connected = match connect_request? {
                    None => return Err(format_err!("BR/EDR ended service registration")),
                    Some(request) => request,
                };
                let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } = connected;
                let peer_id: PeerId = peer_id.into();
                info!("Connected sink {}", peer_id);
                let channel = match channel.try_into() {
                    Ok(chan) => chan,
                    Err(e) => {
                        info!("Peer {}: channel is not usable: {:?}", peer_id, e);
                        continue;
                    }
                };
                if let Err(e) = peers.lock().await.connected(peer_id, channel, false) {
                    info!("Error connecting peer {}: {:?}", peer_id, e);
                }
            },
            results_request = results_requests.try_next() => {
                let result = match results_request? {
                    None => return Err(format_err!("BR/EDR ended service search")),
                    Some(request) => request,
                };
                let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } = result;
                let peer_id: PeerId = peer_id.into();
                info!("Discovered sink {} - protocol {:?}: {:?}", peer_id, protocol, attributes);
                responder.send()?;
                let desc = match find_profile_descriptors(&attributes) {
                    Ok(profiles) => profiles.into_iter().next().expect("at least one profile descriptor"),
                    Err(_) => {
                        info!("Couldn't find profile in peer {} search results, ignoring.", peer_id);
                        continue;
                    }
                };
                peers.lock().await.found(peer_id, desc);

                fasync::Task::local(connect_after_timeout(peer_id, profile.clone(), peers.clone(),  signaling_channel_mode)).detach();
            },
            complete => break,
        }
    }
    Ok(())
}

// Amount of time to wait after we detect a peer before we will initiate a signal connection.
const INITIATOR_DELAY: zx::Duration = zx::Duration::from_seconds(2);

async fn connect_after_timeout(
    id: PeerId,
    profile: bredr::ProfileProxy,
    peers: Arc<Mutex<ConnectedPeers>>,
    channel_mode: bredr::ChannelMode,
) {
    info!("Waiting {:?} to connect to discovered peer {}", INITIATOR_DELAY, id);
    fasync::Timer::new(INITIATOR_DELAY.after_now()).await;
    if peers.lock().await.is_connected(&id) {
        info!("After initiator delay, {} was connected, not connecting..", id);
        return;
    }
    let channel = match profile
        .connect(
            &mut id.into(),
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
            warn!("FIDL error connecting to peer {}: {:?}", id, e);
            return;
        }
        Ok(Err(code)) => {
            info!("Couldn't connect to peer {}: {:?}", id, code);
            return;
        }
        Ok(Ok(channel)) => channel,
    };
    let channel = match channel.try_into() {
        Ok(chan) => chan,
        Err(e) => {
            warn!("No channel from peer {}: {:?}", id, e);
            return;
        }
    };
    if let Err(e) = peers.lock().await.connected(id, channel, true) {
        info!("Error connecting peer {}: {:?}", id, e);
    }
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

        let peers = ConnectedPeers::new(
            stream::Streams::new(),
            CodecNegotiation::build(vec![]).unwrap(),
            profile_proxy.clone(),
            None,
        );

        let handler_fut = handle_profile_events(
            peers,
            connect_stream,
            results_stream,
            profile_proxy,
            bredr::ChannelMode::Basic,
        );
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
