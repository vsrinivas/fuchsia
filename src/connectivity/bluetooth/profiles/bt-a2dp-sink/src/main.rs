// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, async_await, await_macro)]
#![recursion_limit = "256"]

use {
    bt_avdtp as avdtp,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_media::AUDIO_ENCODING_SBC,
    fuchsia_async as fasync,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self as mpsc, Receiver, Sender},
        select, FutureExt, StreamExt,
    },
    parking_lot::RwLock,
    std::{collections::hash_map::Entry, collections::HashMap, string::String, sync::Arc},
};

mod player;

/// When true, the service will display a byte count while streaming.
const DEBUG_STREAM_STATS: bool = false;

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
    fn start(&mut self) -> Result<(), avdtp::ErrorCode> {
        let start_res = self.endpoint.start();
        if start_res.is_err() || self.suspend_sender.is_some() {
            fx_log_info!("Start when streaming: {:?} {:?}", start_res, self.suspend_sender);
            return Err(avdtp::ErrorCode::BadState);
        }
        let (send, receive) = mpsc::channel(1);
        self.suspend_sender = Some(send);
        fuchsia_async::spawn(decode_media_stream(
            self.endpoint.take_transport(),
            self.encoding.clone(),
            receive,
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
}

/// A collection of streams that can be indexed by their EndpointId to their
/// endpoint and the codec to use for this endpoint.
struct Streams(HashMap<avdtp::StreamEndpointId, Stream>);

impl Streams {
    /// A new empty set of endpoints.
    fn new() -> Streams {
        Streams(HashMap::new())
    }

    /// Builds a set of endpoints from the available codecs.
    fn build() -> avdtp::Result<Streams> {
        let mut s = Streams::new();
        // TODO(BT-533): detect codecs, add streams for each codec
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
        Ok(s)
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
}

/// Discovers any remote streams and reports their information to the log.
async fn discover_remote_streams(peer: Arc<avdtp::Peer>) {
    let streams = await!(peer.discover()).expect("Failed to discover source streams");
    fx_log_info!("Discovered {} streams", streams.len());
    for info in streams {
        match await!(peer.get_all_capabilities(info.id())) {
            Ok(capabilities) => {
                fx_log_info!("Stream {:?}", info);
                for cap in capabilities {
                    fx_log_info!("  - {:?}", cap);
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
}

type RemotesMap = HashMap<String, RemotePeer>;

impl RemotePeer {
    fn new(peer: avdtp::Peer) -> RemotePeer {
        RemotePeer { peer: Arc::new(peer), opening: None, streams: Streams::build().unwrap() }
    }

    /// Provides a reference to the AVDTP peer.
    fn peer(&self) -> Arc<avdtp::Peer> {
        self.peer.clone()
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
        fuchsia_async::spawn(
            async move {
                while let Some(r) = await!(request_stream.next()) {
                    match r {
                        Err(e) => fx_log_info!("Request Error on {}: {:?}", device_id, e),
                        Ok(request) => {
                            let mut peer;
                            {
                                let mut wremotes = remotes.write();
                                peer = wremotes.remove(&device_id).unwrap();
                            }
                            let fut = peer.handle_request(request);
                            if let Err(e) = await!(fut) {
                                fx_log_warn!("{} Error handling request: {:?}", device_id, e);
                            }
                            remotes.write().insert(device_id.clone(), peer);
                        }
                    }
                }
                fx_log_info!("Peer {} disconnected", device_id);
                remotes.write().remove(&device_id);
            },
        );
    }

    /// Handle a single request event from the avdtp peer.
    async fn handle_request(&mut self, r: avdtp::Request) -> avdtp::Result<()> {
        fx_log_info!("Handling {:?} from peer..", r);
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
                    Some(stream) => await!(stream.release(responder, &self.peer)),
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
                match stream.configure(&remote_stream_id, capabilities) {
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
                    if let Err(code) = self.streams.get_mut(&seid).and_then(|x| x.start()) {
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
                await!(stream.abort(None))?;
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
) -> () {
    let mut total_bytes = 0;
    let mut player = match await!(player::Player::new(encoding.clone())) {
        Ok(v) => v,
        Err(e) => {
            fx_log_info!("Can't setup stream source for Media: {:?}", e);
            return;
        }
    };
    loop {
        select! {
            item = stream.next().fuse() => {
                if item.is_none() {
                    fx_log_info!("Media transport closed");
                    return;
                }
                match item.unwrap() {
                    Ok(pkt) => {
                        match player.push_payload(&pkt.as_slice()) {
                            Err(e) => {
                                fx_log_info!("can't push packet: {:?}", e);
                            }
                            _ => (),
                        };
                        total_bytes += pkt.len();
                        if !player.playing() {
                            player.play().unwrap_or_else(|e| fx_log_info!("Problem playing: {:}", e));
                        }
                        // TODO(BT-696): Report rx stats to the hub.
                        if DEBUG_STREAM_STATS {
                            eprint!(
                                "Media Packet received: +{} bytes = {} \r",
                                pkt.len(),
                                total_bytes
                                );
                        }
                    }
                    Err(e) => {
                        fx_log_info!("Error in media stream: {:?}", e);
                        return;
                    }
                }
            },
            evt = player.next_event().fuse() => {
                fx_log_info!("Player Event happened: {:?}", evt);
                if evt.is_none() {
                    fx_log_info!("Rebuilding Player: {:?}", evt);
                    // The player died somehow? Attempt to rebuild the player.
                    player = match await!(player::Player::new(encoding.clone())) {
                        Ok(v) => v,
                        Err(e) => {
                            fx_log_info!("Can't rebuild player: {:?}", e);
                            return;
                        }
                    };
                }
            }
            _ = end_signal.next().fuse() => {
                fx_log_info!("Stream ending on end signal");
                return;
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["a2dp-sink"]).expect("Can't init logger");
    let profile_svc = fuchsia_app::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

    let mut service_def = make_profile_service_definition();
    let (status, service_id) = await!(profile_svc.add_service(
        &mut service_def,
        SecurityLevel::EncryptionOptional,
        false
    ))?;

    fx_log_info!("Registered Service ID {}", service_id);

    if let Some(e) = status.error {
        return Err(format_err!("Couldn't add A2DP sink service: {:?}", e));
    }

    let remotes: Arc<RwLock<RemotesMap>> = Arc::new(RwLock::new(HashMap::new()));

    let mut evt_stream = profile_svc.take_event_stream();
    while let Some(evt) = await!(evt_stream.next()) {
        match evt {
            Err(e) => return Err(e.into()),
            Ok(ProfileEvent::OnServiceFound { peer_id, .. }) => {
                fx_log_info!("OnServiceFound on {}, ignoring.", peer_id);
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
                        let remote = entry.insert(RemotePeer::new(peer));
                        // Spawn tasks to handle this remote
                        remote.start_requests_task(remotes.clone(), device_id);
                        fuchsia_async::spawn(discover_remote_streams(remote.peer()));
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
