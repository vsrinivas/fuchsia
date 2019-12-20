// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    argh::FromArgs,
    bt_a2dp::media_types::*,
    bt_avdtp::{self as avdtp, AvdtpControllerPool},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak},
        types::PeerId,
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{self, AsyncWriteExt, StreamExt, TryStreamExt},
    parking_lot::Mutex,
    std::{convert::TryInto, string::String, sync::Arc},
};

mod encoding;
mod pcm_audio;
mod peer;
mod sources;

use crate::encoding::{EncodedStreamSbc, RtpPacketBuilderSbc};
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

/// Builds a set of endpoints from the available codecs.
pub(crate) fn build_local_endpoints() -> avdtp::Result<Vec<avdtp::StreamEndpoint>> {
    // TODO(BT-533): detect codecs, add streams for each codec

    let sbc_codec_info = SbcCodecInfo::new(
        SbcSamplingFrequency::FREQ48000HZ,
        SbcChannelMode::JOINT_STEREO,
        SbcBlockCount::MANDATORY_SRC,
        SbcSubBands::MANDATORY_SRC,
        SbcAllocation::MANDATORY_SRC,
        SbcCodecInfo::BITPOOL_MIN,
        SbcCodecInfo::BITPOOL_MAX,
    )
    .or(Err(avdtp::Error::OutOfRange))?;
    fx_log_info!("Supported codec parameters: {:?}.", sbc_codec_info);

    let sbc_stream = avdtp::StreamEndpoint::new(
        SBC_SEID,
        avdtp::MediaType::Audio,
        avdtp::EndpointType::Source,
        vec![
            avdtp::ServiceCapability::MediaTransport,
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra: sbc_codec_info.to_bytes(),
            },
        ],
    )?;
    Ok(vec![sbc_stream])
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
        let (status, channel) =
            self.profile.connect_l2cap(&id.to_string(), PSM_AVDTP as u16).await?;

        if let Some(e) = status.error {
            fx_log_warn!("Couldn't connect to {}: {:?}", id, e);
            return Ok(());
        }
        match channel {
            Some(channel) => self.connected(id, channel, Some(desc))?,
            None => fx_log_warn!("Couldn't connect {}: no channel", id),
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

/// The number of frames in each SBC packet sent to the peer.
/// 5 is chosen by default as it represents a low amount of latency and fits within the default
/// L2CAP MTU.
/// RTP Header (12 bytes) + 1 byte (SBC header) + 5 * SBC Frame (119 bytes) = 608 bytes < 672
const FRAMES_PER_SBC_PACKET: u8 = 5;

async fn start_streaming(
    peer: &DetachableWeak<PeerId, Peer>,
    source_type: AudioSourceType,
) -> Result<(), failure::Error> {
    let stream_caps_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.collect_capabilities()
    };
    let stream_caps = stream_caps_fut.await?;

    // Find the required SBC stream.
    let (remote_stream_id, _capability) = stream_caps
        .iter()
        .find(|(_id, codec)| codec.is_codec_type(&avdtp::MediaCodecType::AUDIO_SBC))
        .ok_or(format_err!("Couldn't find a compatible stream"))?;

    // TODO(39321): Choose codec options based on availability and quality.
    let sbc_settings = avdtp::ServiceCapability::MediaCodec {
        media_type: avdtp::MediaType::Audio,
        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
        codec_extra: vec![0x11, 0x15, 2, 53],
    };

    let start_stream_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.start_stream(
            SBC_SEID.try_into().unwrap(),
            remote_stream_id.clone(),
            sbc_settings.clone(),
        )
    };

    let mut media_stream = start_stream_fut.await?;

    // This format represents the settings in `sbc_settings` above
    let pcm_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 48000,
        channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
    };

    let source_stream = sources::build_stream(pcm_format.clone(), source_type)?;

    let mut encoded_stream = EncodedStreamSbc::build(pcm_format, &sbc_settings, source_stream)?;

    let mut builder = RtpPacketBuilderSbc::new(FRAMES_PER_SBC_PACKET);

    while let Some(encoded) = encoded_stream.try_next().await? {
        if let Some(packet) =
            builder.push_frame(encoded, encoding::PCM_FRAMES_PER_ENCODED as u32)?
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
    let pool_clone = controller_pool.clone();
    fs.dir("svc").add_fidl_service(move |s| pool_clone.lock().connected(s));
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_warn!("Unable to serve Inspect service directory: {}", e);
    }
    fasync::spawn(fs.collect::<()>());

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

    let mut service_def = make_profile_service_definition();
    let (status, _) =
        profile_svc.add_service(&mut service_def, SecurityLevel::EncryptionOptional, false).await?;

    if let Some(e) = status.error {
        return Err(format_err!("Couldn't add A2DP source service: {:?}", e));
    }

    let attrs: Vec<u16> = vec![
        ATTR_PROTOCOL_DESCRIPTOR_LIST,
        ATTR_SERVICE_CLASS_ID_LIST,
        ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_A2DP_SUPPORTED_FEATURES,
    ];

    profile_svc.add_search(ServiceClassProfileIdentifier::AudioSink, &mut attrs.into_iter())?;
    let mut evt_stream = profile_svc.take_event_stream();

    let mut peers = Peers::new(opts.source, profile_svc);

    while let Some(evt) = evt_stream.next().await {
        let res = match evt? {
            ProfileEvent::OnConnected { device_id, channel, .. } => {
                fx_log_info!("Connected sink {}", device_id);
                let peer_id = device_id.parse().expect("peer ids from profile should parse");
                let connected_res = peers.connected(device_id.parse()?, channel, None);
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
