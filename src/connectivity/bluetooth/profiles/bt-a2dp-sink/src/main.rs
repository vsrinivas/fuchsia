// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    bt_a2dp::media_types::*,
    bt_a2dp_sink_metrics as metrics, bt_avdtp as avdtp,
    failure::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_media::{AUDIO_ENCODING_AACLATM, AUDIO_ENCODING_SBC},
    fuchsia_async as fasync,
    fuchsia_bluetooth::inspect::DebugExt,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::nodes::ManagedNode,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, FutureExt, StreamExt,
    },
    parking_lot::Mutex,
    std::{collections::hash_map, collections::HashMap, sync::Arc},
};

use crate::inspect_types::StreamingInspectData;

mod avdtp_controller;
mod connected_peers;
mod inspect_types;
mod peer;
mod player;

use crate::avdtp_controller::AvdtpControllerPool;

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

// Arbitrarily chosen ID for the SBC stream endpoint.
const SBC_SEID: u8 = 6;

// Arbitrarily chosen ID for the AAC stream endpoint.
const AAC_SEID: u8 = 7;

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

    /// Extract sampling frequency from AAC codec extra data field
    /// (A2DP Sec. 4.5.2)
    fn parse_aac_sampling_frequency(codec_extra: &[u8]) -> u32 {
        if codec_extra.len() != 6 {
            fx_log_warn!("Invalid AAC codec extra length: {:?}", codec_extra.len());
            return DEFAULT_SAMPLE_RATE;
        }

        // AACMediaCodecInfo is represented as 8 bytes, with lower 6 bytes containing
        // the codec extra data.
        let mut codec_info_bytes = [0_u8; 8];
        let codec_info_slice = &mut codec_info_bytes[2..8];

        codec_info_slice.copy_from_slice(&codec_extra);

        let codec_info = AACMediaCodecInfo(u64::from_be_bytes(codec_info_bytes));
        let sample_freq = AACSamplingFrequency::from_bits_truncate(codec_info.sampling_frequency());

        match sample_freq {
            AACSamplingFrequency::FREQ48000HZ => 48000,
            AACSamplingFrequency::FREQ44100HZ => 44100,
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
                    match c {
                        avdtp::ServiceCapability::MediaCodec {
                            media_type: avdtp::MediaType::Audio,
                            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                            codec_extra,
                        } => return Self::parse_sbc_sampling_frequency(&codec_extra),
                        avdtp::ServiceCapability::MediaCodec {
                            media_type: avdtp::MediaType::Audio,
                            codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                            codec_extra,
                        } => return Self::parse_aac_sampling_frequency(&codec_extra),
                        _ => continue,
                    };
                }
                DEFAULT_SAMPLE_RATE
            })
            .unwrap_or(DEFAULT_SAMPLE_RATE)
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

        fuchsia_async::spawn_local(decode_media_stream(
            self.endpoint.take_transport()?,
            self.encoding.clone(),
            self.sample_freq(),
            receive,
            inspect,
            cobalt_sender,
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
        let sbc_media_codec_info = SbcCodecInfo::new(
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
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_media_codec_info.to_bytes(),
                },
            ],
        )?;
        s.insert(sbc_stream, AUDIO_ENCODING_SBC.to_string());

        let aac_media_codec_info = AACMediaCodecInfo::new(
            AACObjectType::MANDATORY_SNK,
            AACSamplingFrequency::MANDATORY_SNK,
            AACChannels::MANDATORY_SNK,
            AACVariableBitRate::MANDATORY_SNK,
            0, // 0 = Unknown constant bitrate support (A2DP Sec. 4.5.2.4)
        )?;
        fx_log_info!("Supported codec parameters: {:?}.", aac_media_codec_info);

        let aac_stream = avdtp::StreamEndpoint::new(
            AAC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: aac_media_codec_info.to_bytes(),
                },
            ],
        )?;
        s.insert(aac_stream, AUDIO_ENCODING_AACLATM.to_string());

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

    fn iter_mut(&mut self) -> hash_map::IterMut<'_, avdtp::StreamEndpointId, Stream> {
        self.0.iter_mut()
    }
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
    cobalt_sender: CobaltSender,
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

    report_stream_metrics(cobalt_sender, &encoding, (end_time - start_time).into_seconds());
}

fn report_stream_metrics(mut cobalt_sender: CobaltSender, encoding: &str, duration_seconds: i64) {
    let codec = match encoding.as_ref() {
        AUDIO_ENCODING_SBC => metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Sbc,
        AUDIO_ENCODING_AACLATM => metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Aac,
        _ => metrics::A2dpStreamDurationInSecondsMetricDimensionCodec::Unknown,
    };

    cobalt_sender.log_elapsed_time(
        metrics::A2DP_STREAM_DURATION_IN_SECONDS_METRIC_ID,
        codec as u32,
        duration_seconds,
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");

    let controller_pool = Arc::new(Mutex::new(AvdtpControllerPool::new()));

    let inspect = inspect::Inspector::new();
    let mut fs = ServiceFs::new();
    inspect.serve(&mut fs)?;

    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.lock().connected(s));

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

    let mut peers = connected_peers::ConnectedPeers::new(streams, cobalt_logger.clone());

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
                peers.found(peer_id, profile);
            }
            Ok(ProfileEvent::OnConnected { device_id, service_id: _, channel, protocol }) => {
                fx_log_info!("Connection from {}: {:?} {:?}!", device_id, channel, protocol);
                let peer_id = device_id.parse().expect("peer ids from profile should parse");
                peers.connected(&inspect, peer_id, channel);
                if let Some(peer) = peers.get(&peer_id) {
                    controller_pool.lock().peer_connected(peer_id, peer.read().avdtp_peer());
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fidl_fuchsia_cobalt::EventPayload;
    use futures::channel::mpsc;
    use matches::assert_matches;

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

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
    /// Test that a AAC endpoint stream works as expected to retrieve codec info
    fn test_aac_stream() {
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
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: vec![128, 1, 4, 4, 226, 0],
                },
            ],
        )
        .expect("Failed to create endpoint");

        let id = s.local_id().clone();
        let information = s.information();
        let encoding = AUDIO_ENCODING_AACLATM.to_string();
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
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        codec_extra: vec![128, 1, 4, 4, 226, 0],
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

    #[test]
    /// Test that cobalt metrics are sent after stream ends
    fn test_cobalt_metrics() {
        let (send, mut recv) = fake_cobalt_sender();
        const TEST_DURATION: i64 = 1;

        report_stream_metrics(send, AUDIO_ENCODING_AACLATM, TEST_DURATION);

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
}
