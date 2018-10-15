// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(futures_api, pin, async_await, arbitrary_self_types, await_macro)]

#[macro_use]
extern crate failure;

use bt_avdtp as avdtp;
use failure::{Error, ResultExt};
use fidl_fuchsia_bluetooth_bredr::*;
use fuchsia_async as fasync;
use fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog};
use fuchsia_zircon as zx;
use futures::{StreamExt, TryFutureExt};
use parking_lot::RwLock;
use std::collections::HashSet;
use std::string::String;
use std::sync::Arc;

/// Make the SDP definidion for the A2DP sink service.
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
        information: vec![],
        additional_attributes: None,
    }
}

fn get_available_stream_info() -> Result<Vec<avdtp::StreamInformation>, avdtp::Error> {
    // TODO(jamuraa): retrieve available streams from Media
    let s = avdtp::StreamInformation::new(
        1,
        false,
        avdtp::MediaType::Audio,
        avdtp::EndpointType::Sink,
    )?;
    Ok(vec![s])
}

async fn discover_remote_streams(peer: Arc<avdtp::Peer>) {
    let streams = await!(peer.discover()).expect("Failed to discover source streams");
    fx_log_info!(tag: "a2dp-sink", "Discovered {} streams", streams.len());
    for info in streams {
        match await!(peer.get_all_capabilities(info.id())) {
            Ok(capabilities) => {
                fx_log_info!(tag: "a2dp-sink", "Stream {:?}", info);
                for cap in capabilities {
                    fx_log_info!(tag: "a2dp-sink", "  - {:?}", cap);
                }
            }
            Err(e) => {
                fx_log_info!(tag: "a2dp-sink", "Stream {} discovery failed: {:?}", info.id(), e)
            }
        };
    }
}

// Defined in the Bluetooth Assigned Numbers for Audio/Video applications
// https://www.bluetooth.com/specifications/assigned-numbers/audio-video
const AUDIO_CODEC_SBC: u8 = 0;

fn get_stream_capabilities(
    _: avdtp::StreamEndpointId,
) -> Result<Vec<avdtp::ServiceCapability>, avdtp::Error> {
    // TODO(jamuraa): get the capabilities of the available stream from the Media Framework
    // This is SBC with minimal requirements for a sink.
    Ok(vec![
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
    ])
}

fn start_media_stream(sock: zx::Socket) -> Result<(), zx::Status> {
    let mut stream_sock = fasync::Socket::from_socket(sock)?;
    fuchsia_async::spawn(
        (async move {
            let mut total_bytes = 0;
            while let Some(item) = await!(stream_sock.next()) {
                match item {
                    Ok(pkt) => {
                        // TODO(jamuraa): decode the media stream into packets and frames
                        // TODO(jamuraa): deliver audio frames to media
                        total_bytes += pkt.len();
                        eprint!(
                            "Media Packet received: +{} bytes = {} \r",
                            pkt.len(),
                            total_bytes
                        );
                    }
                    Err(e) => return Err(e),
                }
            }
            fx_log_info!(tag: "a2dp-sink", "Media stream finished");
            Ok(())
        })
            .unwrap_or_else(|e| fx_log_info!(tag: "a2dp-sink", "Error in media stream: {:?}", e)),
    );
    Ok(())
}

fn handle_request(r: avdtp::Request) -> Result<(), avdtp::Error> {
    fx_vlog!(tag: "a2dp-sink", 1, "Handling {:?} from peer..", r);
    match r {
        avdtp::Request::Discover { responder } => {
            let streams = get_available_stream_info()?;
            responder.send(&streams)
        }
        avdtp::Request::GetCapabilities {
            responder,
            stream_id,
        }
        | avdtp::Request::GetAllCapabilities {
            responder,
            stream_id,
        } => {
            let caps = get_stream_capabilities(stream_id)?;
            responder.send(&caps)
        }
        // Positively respond to everything else.
        avdtp::Request::Open { responder, .. } | avdtp::Request::Close { responder, .. } => {
            responder.send()
        }
        avdtp::Request::Start { responder, .. } | avdtp::Request::Suspend { responder, .. } => {
            responder.send()
        }
    }
}

async fn handle_requests(
    peer: Arc<avdtp::Peer>, remotes: Arc<RwLock<HashSet<String>>>, device_id: String,
) {
    let mut stream = peer.take_request_stream();
    while let Some(r) = await!(stream.next()) {
        match r {
            Err(e) => {
                fx_log_info!(tag: "a2dp-sink", "Request Error: {:?}", e);
            }
            Ok(request) => {
                if let Err(e) = handle_request(request) {
                    fx_log_warn!(tag: "a2dp-sink", "Error handling request: {:?}", e);
                }
            }
        }
    }
    fx_log_info!(tag: "a2dp-sink", "Peer {} disconnected", device_id);
    remotes.write().remove(&device_id);
}

async fn init() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");
    let profile_svc = fuchsia_app::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

    let mut service_def = make_profile_service_definition();
    let (status, service_id) = await!(profile_svc.add_service(
        &mut service_def,
        SecurityLevel::EncryptionOptional,
        false
    ))?;

    fx_log_info!(tag: "a2dp-sink", "Registered Service ID {}", service_id);

    if let Some(e) = status.error {
        return Err(format_err!("Couldn't add A2DP sink service: {:?}", e));
    }

    let remotes: Arc<RwLock<HashSet<String>>> = Arc::new(RwLock::new(HashSet::new()));
    let mut evt_stream = profile_svc.take_event_stream();
    while let Some(evt) = await!(evt_stream.next()) {
        if evt.is_err() {
            return Err(evt.err().unwrap().into());
        }
        match evt.unwrap() {
            ProfileEvent::OnConnected {
                device_id,
                service_id: _,
                channel,
                protocol,
            } => {
                fx_log_info!(tag: "a2dp-sink", "Connection from {}: {:?} {:?}!", device_id, channel, protocol);
                if remotes.read().contains(&device_id) {
                    fx_log_info!(tag: "a2dp-sink", "{} already connected: connecting media channel", device_id);
                    let _ = start_media_stream(channel);
                    continue;
                }
                fx_vlog!(tag: "a2dp-sink", 1, "Adding new peer for {}", device_id);
                let peer = match avdtp::Peer::new(channel) {
                    Ok(peer) => Arc::new(peer),
                    Err(e) => {
                        fx_log_warn!(tag: "a2dp-sink", "Error adding peer: {:?}", e);
                        continue;
                    }
                };
                // Spawn threads to handle this peer
                fuchsia_async::spawn(handle_requests(
                    peer.clone(),
                    remotes.clone(),
                    device_id.clone(),
                ));
                fuchsia_async::spawn(discover_remote_streams(peer.clone()));
                remotes.write().insert(device_id);
            }
        }
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    let mut executor = fuchsia_async::Executor::new().context("error creating executor")?;

    executor.run_singlethreaded(init())
}
