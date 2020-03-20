// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    async_helpers::component_lifecycle::ComponentLifecycleServer,
    bt_a2dp::{codec::MediaCodecConfig, media_types::*},
    bt_avdtp::{self as avdtp, AvdtpControllerPool},
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_bluetooth_component::LifecycleState,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak},
        types::PeerId,
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{self, AsyncWriteExt, StreamExt, TryStreamExt},
    parking_lot::Mutex,
    std::{
        convert::{TryFrom, TryInto},
        string::String,
        sync::Arc,
    },
};

mod encoding;
mod pcm_audio;
mod peer;
mod sources;

use crate::encoding::{EncodedStream, RtpPacketBuilder};
use crate::pcm_audio::PcmAudio;
use crate::peer::Peer;
use sources::AudioSourceType;

/// Make the SDP definition for the A2DP source service.
fn make_profile_service_definition() -> ServiceDefinition {
    ServiceDefinition {
        service_class_uuids: vec![String::from("110A")], // Audio Source UUID
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
            minor_version: 2,
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

// Highest AAC bitrate we want to transmit
const MAX_BITRATE_AAC: u32 = 250000;

/// Builds a set of endpoints from the available codecs.
pub(crate) fn build_local_endpoints() -> avdtp::Result<Vec<avdtp::StreamEndpoint>> {
    // TODO(BT-533): detect codecs, add streams for each codec

    let mut streams: Vec<avdtp::StreamEndpoint> = Vec::new();

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

    let sbc_stream = avdtp::StreamEndpoint::new(
        SBC_SEID,
        avdtp::MediaType::Audio,
        avdtp::EndpointType::Source,
        vec![
            avdtp::ServiceCapability::MediaTransport,
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra: sbc_codec_info.to_bytes().to_vec(),
            },
        ],
    )?;
    streams.push(sbc_stream);

    let aac_codec_info = AacCodecInfo::new(
        AacObjectType::MANDATORY_SRC,
        AacSamplingFrequency::FREQ48000HZ,
        AacChannels::TWO,
        true,
        MAX_BITRATE_AAC,
    )?;

    fx_vlog!(1, "Supported AAC codec parameters: {:?}.", aac_codec_info);

    let aac_stream = avdtp::StreamEndpoint::new(
        AAC_SEID,
        avdtp::MediaType::Audio,
        avdtp::EndpointType::Source,
        vec![
            avdtp::ServiceCapability::MediaTransport,
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                codec_extra: aac_codec_info.to_bytes().to_vec(),
            },
        ],
    )?;
    streams.push(aac_stream);

    Ok(streams)
}

struct Peers {
    peers: DetachableMap<PeerId, Peer>,
    source_type: AudioSourceType,
    profile: ProfileProxy,
}

impl Peers {
    fn new(source_type: AudioSourceType, profile: ProfileProxy) -> Self {
        Peers { peers: DetachableMap::new(), source_type, profile }
    }

    pub(crate) fn get(&self, id: &PeerId) -> Option<Arc<peer::Peer>> {
        self.peers.get(id).and_then(|p| p.upgrade())
    }

    async fn discovered(&mut self, id: PeerId, desc: ProfileDescriptor) -> Result<(), Error> {
        if let Some(peer) = self.peers.get(&id) {
            if let Some(peer) = peer.upgrade() {
                if let None = peer.set_descriptor(desc.clone()) {
                    self.spawn_streaming(id);
                }
            }
            return Ok(());
        }
        let (status, channel) = self
            .profile
            .connect_l2cap(&id.to_string(), PSM_AVDTP as u16, ChannelParameters::new_empty())
            .await?;

        if let Some(e) = status.error {
            fx_log_warn!("Couldn't connect to {}: {:?}", id, e);
            return Ok(());
        }
        match channel.socket {
            Some(socket) => self.connected(id, socket, Some(desc))?,
            None => fx_log_warn!("Couldn't connect {}: no socket", id),
        };
        Ok(())
    }

    fn connected(
        &mut self,
        id: PeerId,
        channel: zx::Socket,
        desc: Option<ProfileDescriptor>,
    ) -> Result<(), Error> {
        if let Some(peer) = self.peers.get(&id) {
            if let Some(peer) = peer.upgrade() {
                if let Err(e) = peer.receive_channel(channel) {
                    fx_log_warn!("{} connected an unexpected channel: {}", id, e);
                }
            }
        } else {
            let avdtp_peer =
                avdtp::Peer::new(channel).map_err(|e| avdtp::Error::ChannelSetup(e))?;
            let endpoints = build_local_endpoints()?;
            let peer = Peer::create(id, avdtp_peer, endpoints, self.profile.clone());
            // Start the streaming task if the profile information is populated.
            // Otherwise, `self.discovered()` will do so.
            let start_streaming_flag = desc.map_or(false, |d| {
                peer.set_descriptor(d);
                true
            });
            self.peers.insert(id, peer);

            if start_streaming_flag {
                self.spawn_streaming(id);
            }
        }
        Ok(())
    }

    fn spawn_streaming(&mut self, id: PeerId) {
        let weak_peer = self.peers.get(&id).expect("just added");
        let source_type = self.source_type.clone();
        fuchsia_async::spawn_local(async move {
            if let Err(e) = start_streaming(&weak_peer, source_type).await {
                fx_log_info!("Failed to stream: {:?}", e);
                weak_peer.detach();
            }
        });
    }
}

/// Pick a reasonable quality bitrate to use by default. 64k average per channel.
const PREFERRED_BITRATE_AAC: u32 = 128000;

/// Represents a chosen remote stream endpoint and negotiated codec and encoder settings
#[derive(Debug)]
struct SelectedStream<'a> {
    remote_stream: &'a avdtp::StreamEndpoint,
    codec_settings: avdtp::ServiceCapability,
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
    ) -> Result<avdtp::ServiceCapability, Error> {
        match stream.codec_type() {
            Some(&avdtp::MediaCodecType::AUDIO_AAC) => {
                let codec_extra =
                    Self::lookup_codec_extra(stream).ok_or(format_err!("no codec extra found"))?;
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
                Ok(avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: aac_codec_info.to_bytes().to_vec(),
                })
            }
            Some(&avdtp::MediaCodecType::AUDIO_SBC) => {
                // TODO(39321): Choose codec options based on availability and quality.
                let sbc_codec_info = SbcCodecInfo::new(
                    SbcSamplingFrequency::FREQ48000HZ,
                    SbcChannelMode::JOINT_STEREO,
                    SbcBlockCount::SIXTEEN,
                    SbcSubBands::EIGHT,
                    SbcAllocation::LOUDNESS,
                    /* min_bpv= */ 53,
                    /* max_bpv= */ 53,
                )?;

                Ok(avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                })
            }
            Some(t) => Err(format_err!("Unsupported codec {:?}", t)),
            None => Err(format_err!("No codec type")),
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

    fn lookup_codec_extra(stream: &'a avdtp::StreamEndpoint) -> Option<&'a Vec<u8>> {
        stream.capabilities().iter().find_map(|cap| match cap {
            avdtp::ServiceCapability::MediaCodec { codec_extra, .. } => Some(codec_extra),
            _ => None,
        })
    }
}

async fn start_streaming(
    peer: &DetachableWeak<PeerId, Peer>,
    source_type: AudioSourceType,
) -> Result<(), anyhow::Error> {
    let streams_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.collect_capabilities()
    };
    let remote_streams = streams_fut.await?;
    let selected_stream = SelectedStream::pick(&remote_streams)?;

    let start_stream_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.start_stream(
            selected_stream.seid.try_into()?,
            selected_stream.remote_stream.local_id().clone(),
            selected_stream.codec_settings.clone(),
        )
    };

    let mut media_stream = start_stream_fut.await?;

    // all sinks must support these options
    let pcm_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 48000,
        channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
    };

    let source_stream = sources::build_stream(&peer.key(), pcm_format.clone(), source_type)?;
    let codec_config = MediaCodecConfig::try_from(&selected_stream.codec_settings)?;
    let mut encoded_stream = EncodedStream::build(pcm_format, source_stream, &codec_config)?;

    let mut builder = RtpPacketBuilder::new(
        codec_config.frames_per_packet() as u8,
        codec_config.rtp_frame_header().to_vec(),
    );

    while let Some(encoded) = encoded_stream.try_next().await? {
        if let Some(packet) =
            builder.push_frame(encoded, codec_config.pcm_frames_per_encoded_frame() as u32)?
        {
            if let Err(e) = media_stream.write(&packet).await {
                fx_log_info!("Failed sending packet to peer: {}", e);
                return Ok(());
            }
        }
    }
    Ok(())
}

/// Defines the options available from the command line
#[derive(FromArgs)]
#[argh(description = "Bluetooth Advanced Audio Distribution Profile: Source")]
struct Opt {
    #[argh(option, default = "AudioSourceType::AudioOut")]
    /// audio source. options: [audio_out, big_ben]. Defaults to 'audio_out'
    source: AudioSourceType,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get the command line args.
    let opts: Opt = argh::from_env();

    fuchsia_syslog::init_with_tags(&["a2dp-source"]).expect("Can't init logger");

    let controller_pool = Arc::new(Mutex::new(AvdtpControllerPool::new()));

    let mut fs = ServiceFs::new();

    let mut lifecycle = ComponentLifecycleServer::spawn();
    fs.dir("svc").add_fidl_service(lifecycle.fidl_service());

    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.lock().connected(s));
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_warn!("Unable to serve Inspect service directory: {}", e);
    }

    fasync::spawn(fs.collect::<()>());

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

    let mut service_def = make_profile_service_definition();

    let (status, _) = profile_svc
        .add_service(
            &mut service_def,
            SecurityLevel::EncryptionOptional,
            ChannelParameters::new_empty(),
        )
        .await?;

    if let Some(e) = status.error {
        return Err(format_err!("Couldn't add A2DP source service: {:?}", e));
    }

    const ATTRS: [u16; 4] = [
        ATTR_PROTOCOL_DESCRIPTOR_LIST,
        ATTR_SERVICE_CLASS_ID_LIST,
        ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];

    profile_svc.add_search(ServiceClassProfileIdentifier::AudioSink, &ATTRS)?;
    let mut evt_stream = profile_svc.take_event_stream();

    let mut peers = Peers::new(opts.source, profile_svc);

    lifecycle.set(LifecycleState::Ready).await.expect("lifecycle server to set value");

    while let Some(evt) = evt_stream.next().await {
        let res = match evt? {
            ProfileEvent::OnConnected { device_id, channel, .. } => {
                fx_log_info!("Connected sink {}", device_id);
                let peer_id = device_id.parse().expect("peer ids from profile should parse");
                let socket = channel.socket.expect("socket from profile should not be None");
                let connected_res = peers.connected(device_id.parse()?, socket, None);
                if let Some(peer) = peers.get(&peer_id) {
                    controller_pool.lock().peer_connected(peer_id, peer.avdtp_peer());
                }
                connected_res
            }
            ProfileEvent::OnServiceFound { peer_id, profile, attributes } => {
                fx_log_info!(
                    "Discovered sink {} - profile {:?}: {:?}",
                    peer_id,
                    profile,
                    attributes
                );
                peers.discovered(peer_id.parse()?, profile).await
            }
        };
        if let Err(e) = res {
            fx_log_warn!("Error processing event: {}", e);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn test_stream_selection() {
        // test that aac is preferred
        let remote_streams = vec![
            avdtp::StreamEndpoint::new(
                AAC_SEID,
                avdtp::MediaType::Audio,
                avdtp::EndpointType::Sink,
                vec![
                    avdtp::ServiceCapability::MediaTransport,
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        codec_extra: vec![0, 0, 0, 0, 0, 0],
                    },
                ],
            )
            .expect("stream endpoint"),
            avdtp::StreamEndpoint::new(
                SBC_SEID,
                avdtp::MediaType::Audio,
                avdtp::EndpointType::Sink,
                vec![
                    avdtp::ServiceCapability::MediaTransport,
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        codec_extra: vec![0, 0, 0, 0],
                    },
                ],
            )
            .expect("stream endpoint"),
        ];

        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: AAC_SEID,
                codec_settings:
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        ..
                    },
                ..
            })
        );

        // only sbc available, should return sbc
        let remote_streams = vec![avdtp::StreamEndpoint::new(
            SBC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: vec![0, 0, 0, 0],
                },
            ],
        )
        .expect("stream endpoint")];

        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: SBC_SEID,
                codec_settings:
                    avdtp::ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        ..
                    },
                ..
            })
        );

        // none available, should error
        let remote_streams = vec![];
        assert_matches!(SelectedStream::pick(&remote_streams), Err(Error { .. }));
    }
}
