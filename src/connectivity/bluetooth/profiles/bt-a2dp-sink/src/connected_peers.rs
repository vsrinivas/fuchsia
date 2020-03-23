// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_a2dp::media_types::*,
    bt_a2dp_sink_metrics as metrics, bt_avdtp as avdtp,
    fidl_fuchsia_bluetooth_bredr::{Channel, ProfileDescriptor, ProfileProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak},
        types::PeerId,
    },
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect as inspect,
    fuchsia_syslog::{fx_log_info, fx_log_warn, fx_vlog},
    parking_lot::RwLock,
    std::{
        collections::hash_map::Entry,
        collections::{HashMap, HashSet},
        convert::TryInto,
        sync::Arc,
    },
};

use crate::{avrcp_relay::AvrcpRelay, peer, Streams, SBC_SEID};

fn codectype_to_availability_metric(
    codec_type: avdtp::MediaCodecType,
) -> metrics::A2dpCodecAvailabilityMetricDimensionCodec {
    match codec_type {
        avdtp::MediaCodecType::AUDIO_SBC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Sbc,
        avdtp::MediaCodecType::AUDIO_MPEG12 => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::Mpeg12
        }
        avdtp::MediaCodecType::AUDIO_AAC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Aac,
        avdtp::MediaCodecType::AUDIO_ATRAC => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::Atrac
        }
        avdtp::MediaCodecType::AUDIO_NON_A2DP => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::VendorSpecific
        }
        _ => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Unknown,
    }
}

fn spawn_stream_discovery(peer: &peer::Peer) {
    let collect_fut = peer.collect_capabilities();
    let remote_capabilities_inspect = peer.remote_capabilities_inspect();
    let mut cobalt = peer.cobalt_logger();

    let discover_fut = async move {
        // Store deduplicated set of codec event codes for logging.
        let mut codec_event_codes = HashSet::new();

        let streams = match collect_fut.await {
            Ok(streams) => streams,
            Err(e) => {
                fx_log_info!("Collecting capabilities failed: {:?}", e);
                return;
            }
        };

        for stream in streams {
            let capabilities = stream.capabilities();
            remote_capabilities_inspect.append(stream.local_id(), &capabilities).await;
            for cap in capabilities {
                if let avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type,
                    ..
                } = cap
                {
                    codec_event_codes
                        .insert(codectype_to_availability_metric(codec_type.clone()) as u32);
                }
            }
        }

        for event_code in codec_event_codes {
            cobalt.log_event(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event_code);
        }
    };
    fasync::spawn(discover_fut);
}

/// ConnectedPeers owns the set of connected peers and manages peers based on
/// discovery, connections and disconnections.
pub struct ConnectedPeers {
    /// The set of connected peers.
    connected: DetachableMap<PeerId, RwLock<peer::Peer>>,
    /// ProfileDescriptors from discovering the peer.
    descriptors: HashMap<PeerId, Option<ProfileDescriptor>>,
    /// The set of streams that are made available to peers.
    streams: Streams,
    /// Profile Proxy, used to connect new transport sockets.
    profile: ProfileProxy,
    /// Cobalt logger to use and hand out to peers
    cobalt_sender: CobaltSender,
    /// Media session domain
    domain: Option<String>,
    /// The 'peers' node of the inspect tree. All connected peers own a child node of this node.
    inspect: inspect::Node,
}

impl ConnectedPeers {
    pub(crate) fn new(
        streams: Streams,
        profile: ProfileProxy,
        cobalt_sender: CobaltSender,
        inspect: inspect::Node,
        domain: Option<String>,
    ) -> Self {
        Self {
            connected: DetachableMap::new(),
            descriptors: HashMap::new(),
            streams,
            profile,
            inspect,
            cobalt_sender,
            domain,
        }
    }

    pub(crate) fn get(&self, id: &PeerId) -> Option<Arc<RwLock<peer::Peer>>> {
        self.connected.get(id).and_then(|p| p.upgrade())
    }

    pub fn is_connected(&self, id: &PeerId) -> bool {
        self.connected.contains_key(id)
    }

    async fn start_streaming(
        peer: &DetachableWeak<PeerId, RwLock<peer::Peer>>,
    ) -> Result<(), anyhow::Error> {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        let remote_streams = strong.read().collect_capabilities().await?;

        // Find the SBC stream, which should exist (it is required)
        // TODO(39321): Prefer AAC when remote peer supports AAC.
        let remote_stream = remote_streams
            .iter()
            .filter(|stream| stream.information().endpoint_type() == &avdtp::EndpointType::Source)
            .find(|stream| stream.codec_type() == Some(&avdtp::MediaCodecType::AUDIO_SBC))
            .ok_or(format_err!("Couldn't find a compatible stream"))?;

        // TODO(39321): Choose codec options based on availability and quality.
        let sbc_media_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::SIXTEEN,
            SbcSubBands::EIGHT,
            SbcAllocation::LOUDNESS,
            SbcCodecInfo::BITPOOL_MIN,
            53, // Commonly used upper bound for bitpool value.
        )?;

        let sbc_settings = avdtp::ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: sbc_media_codec_info.to_bytes().to_vec(),
        };

        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        let _ = strong
            .read()
            .start_stream(
                SBC_SEID.try_into().unwrap(),
                remote_stream.local_id().clone(),
                sbc_settings.clone(),
            )
            .await?;
        Ok(())
    }

    pub fn found(&mut self, id: PeerId, desc: ProfileDescriptor) {
        if self.descriptors.insert(id, Some(desc)).is_some() {
            // We have maybe connected to this peer before, and we just need to
            // discover the streams.
            if let Some(peer) = self.get(&id) {
                peer.write().set_descriptor(desc.clone());
                spawn_stream_discovery(&peer.read());
            }
        }
    }

    pub fn connected(&mut self, id: PeerId, channel: Channel, initiate_streaming: bool) {
        let socket = match channel.socket {
            Some(socket) => socket,
            None => {
                fx_log_warn!("Peer {} delivered a channel with no socket", id);
                return;
            }
        };
        match self.get(&id) {
            Some(peer) => {
                if let Err(e) = peer.write().receive_channel(socket) {
                    fx_log_warn!("{} failed to connect channel: {}", id, e);
                }
            }
            None => {
                fx_log_info!("Adding new peer for {}", id);
                let avdtp_peer = match avdtp::Peer::new(socket) {
                    Ok(peer) => peer,
                    Err(e) => {
                        fx_log_warn!("Error adding signaling peer {}: {:?}", id, e);
                        return;
                    }
                };

                let mut peer = peer::Peer::create(
                    id,
                    avdtp_peer,
                    self.streams.clone(),
                    self.profile.clone(),
                    self.inspect.create_child(&format!("peer {}", id)),
                    self.cobalt_sender.clone(),
                );

                // Start remote discovery if profile information exists for the device_id
                // and a2dp sink not assuming the INT role.
                match self.descriptors.entry(id) {
                    Entry::Occupied(entry) => {
                        if let Some(prof) = entry.get() {
                            peer.set_descriptor(prof.clone());
                            if !initiate_streaming {
                                spawn_stream_discovery(&peer);
                            }
                        }
                    }
                    // Otherwise just insert the device ID with no profile
                    // Run discovery when profile is updated
                    Entry::Vacant(entry) => {
                        entry.insert(None);
                    }
                }
                let closed_fut = peer.closed();
                self.connected.insert(id, RwLock::new(peer));

                let avrcp_relay = AvrcpRelay::start(id, self.domain.clone()).ok();

                if initiate_streaming {
                    let weak_peer = self.connected.get(&id).expect("just added");
                    fuchsia_async::spawn_local(async move {
                        if let Err(e) = ConnectedPeers::start_streaming(&weak_peer).await {
                            fx_vlog!(tag: "a2dp-sink", 1, "Streaming task ended: {:?}", e);
                            weak_peer.detach();
                        }
                    });
                }

                // Remove the peer when we disconnect.
                let detached_peer = self.connected.get(&id).expect("just added");
                let mut descriptors = self.descriptors.clone();
                let disconnected_id = id.clone();
                fasync::spawn(async move {
                    closed_fut.await;
                    detached_peer.detach();
                    descriptors.remove(&disconnected_id);
                    // Captures the relay to extend the lifetime until after the peer clooses.
                    drop(avrcp_relay);
                });
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use bt_avdtp::Request;
    use fidl::encoding::Decodable;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::{
        ProfileMarker, ProfileRequestStream, ServiceClassProfileIdentifier,
    };
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::{self, task::Poll, StreamExt};
    use std::convert::TryFrom;

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn run_to_stalled(exec: &mut fasync::Executor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    fn exercise_avdtp(exec: &mut fasync::Executor, remote: zx::Socket, peer: &peer::Peer) {
        let remote_avdtp = avdtp::Peer::new(remote).expect("remote control should be creatable");
        let mut remote_requests = remote_avdtp.take_request_stream();

        // Should be able to actually communicate via the peer.
        let avdtp = peer.avdtp_peer();
        let discover_fut = avdtp.discover();

        futures::pin_mut!(discover_fut);

        assert!(exec.run_until_stalled(&mut discover_fut).is_pending());

        let responder = match exec.run_until_stalled(&mut remote_requests.next()) {
            Poll::Ready(Some(Ok(Request::Discover { responder }))) => responder,
            x => panic!("Expected a Ready Discovery request but got {:?}", x),
        };

        let endpoint_id = avdtp::StreamEndpointId::try_from(1).expect("endpointid creation");

        let information = avdtp::StreamInformation::new(
            endpoint_id,
            false,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Source,
        );

        responder.send(&[information]).expect("Sending response should have worked");

        let _stream_infos = match exec.run_until_stalled(&mut discover_fut) {
            Poll::Ready(Ok(infos)) => infos,
            x => panic!("Expected a Ready response but got {:?}", x),
        };
    }

    fn setup_connected_peer_test(
    ) -> (fasync::Executor, PeerId, ConnectedPeers, ProfileRequestStream) {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (proxy, stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let id = PeerId(1);
        let (cobalt_sender, _) = fake_cobalt_sender();
        let inspect = inspect::Inspector::new();

        let peers = ConnectedPeers::new(
            Streams::new(),
            proxy,
            cobalt_sender,
            inspect.root().create_child("connected"),
            None,
        );

        (exec, id, peers, stream)
    }

    fn new_channel() -> (zx::Socket, Channel) {
        let (remote, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let chan = Channel { socket: Some(signaling), ..Channel::new_empty() };
        (remote, chan)
    }

    #[test]
    fn connected_peers_connect_creates_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = new_channel();

        peers.connected(id, channel, false);

        let peer = match peers.get(&id) {
            None => panic!("Peer should be in ConnectedPeers after connection"),
            Some(peer) => peer,
        };

        exercise_avdtp(&mut exec, remote, &peer.read());
    }

    fn expect_started_discovery(exec: &mut fasync::Executor, remote: zx::Socket) {
        let remote_avdtp = avdtp::Peer::new(remote).expect("remote control should be creatable");
        let mut remote_requests = remote_avdtp.take_request_stream();

        // Start of discovery is by discovering the peer streaminformations.
        let _ = match exec.run_until_stalled(&mut remote_requests.next()) {
            Poll::Ready(Some(Ok(Request::Discover { responder }))) => responder,
            x => panic!("Expected to get a discovery request but got {:?}", x),
        };
    }

    #[test]
    fn connected_peers_found_connected_peer_starts_discovery() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = new_channel();

        peers.connected(id, channel, false);

        let profile_desc = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };

        peers.found(id, profile_desc);

        expect_started_discovery(&mut exec, remote);
    }

    #[test]
    fn connected_peers_connected_found_peer_starts_discovery() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let profile_desc = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };

        peers.found(id, profile_desc);

        let (remote, channel) = new_channel();
        peers.connected(id, channel, false);

        expect_started_discovery(&mut exec, remote);
    }

    #[test]
    fn connected_peers_peer_disconnect_removes_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = new_channel();

        peers.connected(id, channel, false);
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());
    }

    #[test]
    fn connected_peers_reconnect_works() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = new_channel();
        peers.connected(id, channel, false);
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());

        // Connect another peer with the same ID
        let (_remote, channel) = new_channel();

        peers.connected(id, channel, false);
        run_to_stalled(&mut exec);

        // Should be connected.
        assert!(peers.get(&id).is_some());
    }
}
