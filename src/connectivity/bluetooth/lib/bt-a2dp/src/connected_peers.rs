// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_avdtp as avdtp,
    fidl_fuchsia_bluetooth_bredr::{self as bredr, ChannelMode, ProfileDescriptor, ProfileProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak},
        inspect::DebugExt,
        types::{Channel, PeerId},
    },
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_inspect_derive::{AttachError, Inspect},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        stream::{Stream, StreamExt},
        task::{Context, Poll},
        Future,
    },
    log::{info, warn},
    std::{
        collections::{hash_map::Entry, HashMap, HashSet},
        convert::TryInto,
        pin::Pin,
        sync::Arc,
    },
};

use crate::{codec::CodecNegotiation, peer::Peer, permits::Permits, stream::Streams};

/// Statistics node for tracking various information about a peer that has been encountered.
/// Typically used as an inspect tree node.
struct PeerStats {
    id: PeerId,
    inspect_node: inspect::Node,
    /// The number of times that this peer has been successfully connected to since discovery.
    connection_count: inspect::UintProperty,
}

impl PeerStats {
    fn new(id: PeerId) -> Self {
        Self { id, inspect_node: Default::default(), connection_count: Default::default() }
    }

    fn set_descriptor(&mut self, descriptor: &ProfileDescriptor) {
        self.inspect_node.record_string("descriptor", descriptor.debug());
    }

    fn record_connected(&mut self) {
        self.connection_count.add(1);
    }
}

impl Inspect for &mut PeerStats {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.inspect_node.record_string("id", self.id.to_string());
        self.connection_count = self.inspect_node.create_uint("connection_count", 0);
        Ok(())
    }
}

#[derive(Default)]
struct DiscoveredPeers {
    /// The peers that we have discovered, with their descriptors and potential preferred
    /// endpoint directions. Because the same peer can be discovered multiple times, with
    /// potentially different endpoints, we maintain a set of advertised directions.
    descriptors: HashMap<PeerId, (ProfileDescriptor, HashSet<avdtp::EndpointType>)>,
    /// Holds the child nodes which include the ids and profile descriptors for inspect.
    stats: HashMap<PeerId, PeerStats>,
    /// Inspect node, usually at "discovered" in the tree.
    inspect_node: inspect::Node,
}

impl DiscoveredPeers {
    fn insert(
        &mut self,
        id: PeerId,
        descriptor: ProfileDescriptor,
        directions: HashSet<avdtp::EndpointType>,
    ) {
        match self.descriptors.entry(id) {
            Entry::Occupied(mut entry) => {
                entry.get_mut().0 = descriptor;
                entry.get_mut().1.extend(&directions);
            }
            Entry::Vacant(entry) => {
                let _ = entry.insert((descriptor, directions));
            }
        };
        self.stats
            .entry(id)
            .or_insert({
                let mut new_stats = PeerStats::new(id);
                let _ = new_stats.iattach(&self.inspect_node, inspect::unique_name("peer_"));
                new_stats
            })
            .set_descriptor(&descriptor);
    }

    fn connected(&mut self, id: PeerId) {
        self.stats.get_mut(&id).map(|stats| stats.record_connected());
    }

    /// Returns the descriptor and preferred endpoint direction associated with the peer `id`.
    fn get(&self, id: &PeerId) -> Option<(ProfileDescriptor, Option<avdtp::EndpointType>)> {
        self.descriptors.get(id).map(|(desc, dirs)| (desc.clone(), find_preferred_direction(dirs)))
    }
}

impl Inspect for &mut DiscoveredPeers {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        Ok(())
    }
}

/// Given a set of endpoint `directions`, returns the preferred direction or None
/// if both Sink and Source are specified.
fn find_preferred_direction(
    directions: &HashSet<avdtp::EndpointType>,
) -> Option<avdtp::EndpointType> {
    if directions.len() == 1 {
        directions.iter().next().cloned()
    } else {
        // Otherwise, either there are no A2DP services or both Sink & Source are specified
        // in which case there is no preferred direction.
        None
    }
}

/// ConnectedPeers manages the set of connected peers based on discovery, new connection, and
/// peer session lifetime.
pub struct ConnectedPeers {
    /// The set of connected peers.
    connected: DetachableMap<PeerId, Peer>,
    /// ProfileDescriptors from discovering the peer, stored here even if the peer is disconnected
    discovered: DiscoveredPeers,
    /// A set of streams which can be used as a template for each newly connected peer.
    streams: Streams,
    /// The permits that each peer uses to validate that we can start a stream.
    permits: Permits,
    /// Codec Negotiation used to choose a compatible stream pair when starting streaming.
    codec_negotiation: CodecNegotiation,
    /// Profile Proxy, used to connect new transport sockets.
    profile: ProfileProxy,
    /// Cobalt logger to use and hand out to peers, if we are using one.
    cobalt_sender: Option<CobaltSender>,
    /// The 'peers' node of the inspect tree. All connected peers own a child node of this node.
    inspect: inspect::Node,
    /// Inspect node for which is the current preferred peer direction.
    inspect_peer_direction: inspect::StringProperty,
    /// Listeners for new connected peers
    connected_peer_senders: Vec<mpsc::Sender<DetachableWeak<PeerId, Peer>>>,
    /// Task handles for newly connected peer stream starts.
    // TODO(fxbug.dev/67947): Completed tasks aren't garbage-collected yet.
    start_stream_tasks: HashMap<PeerId, fasync::Task<()>>,
}

impl ConnectedPeers {
    pub fn new(
        streams: Streams,
        codec_negotiation: CodecNegotiation,
        permits: Permits,
        profile: ProfileProxy,
        cobalt_sender: Option<CobaltSender>,
    ) -> Self {
        Self {
            connected: DetachableMap::new(),
            discovered: DiscoveredPeers::default(),
            streams,
            codec_negotiation,
            profile,
            permits,
            inspect: inspect::Node::default(),
            inspect_peer_direction: inspect::StringProperty::default(),
            cobalt_sender,
            connected_peer_senders: Vec::new(),
            start_stream_tasks: HashMap::new(),
        }
    }

    pub(crate) fn get_weak(&self, id: &PeerId) -> Option<DetachableWeak<PeerId, Peer>> {
        self.connected.get(id)
    }

    pub(crate) fn get(&self, id: &PeerId) -> Option<Arc<Peer>> {
        self.get_weak(id).and_then(|p| p.upgrade())
    }

    pub fn is_connected(&self, id: &PeerId) -> bool {
        self.connected.contains_key(id)
    }

    /// Attempts to start streaming on `peer` by collecting the remote streaming endpoint
    /// information, selecting a compatible peer using `negotiation` and starting the stream.
    /// Does nothing and returns Ok(()) if the peer is already streaming or will start streaming
    /// on it's own.
    async fn start_streaming(
        peer: &DetachableWeak<PeerId, Peer>,
        negotiation: CodecNegotiation,
    ) -> Result<(), anyhow::Error> {
        let remote_streams = {
            let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
            if strong.streaming_active() {
                return Ok(());
            }
            strong.collect_capabilities()
        }
        .await?;

        let (negotiated, remote_seid) =
            negotiation.select(&remote_streams).ok_or(format_err!("No compatible stream found"))?;

        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        if strong.streaming_active() {
            return Ok(());
        }
        strong.stream_start(remote_seid, negotiated).await.map_err(Into::into)
    }

    pub fn found(
        &mut self,
        id: PeerId,
        desc: ProfileDescriptor,
        preferred_directions: HashSet<avdtp::EndpointType>,
    ) {
        self.discovered.insert(id, desc.clone(), preferred_directions);
        self.get(&id).map(|p| p.set_descriptor(desc));
    }

    pub fn set_preferred_direction(&mut self, direction: avdtp::EndpointType) {
        self.codec_negotiation.set_direction(direction);
        self.inspect_peer_direction.set(&format!("{:?}", direction));
    }

    pub fn preferred_direction(&self) -> avdtp::EndpointType {
        self.codec_negotiation.direction()
    }

    pub fn try_connect(
        &self,
        id: PeerId,
        channel_mode: ChannelMode,
    ) -> impl Future<Output = Result<Option<Channel>, Error>> {
        let proxy = self.profile.clone();
        let connected = self.is_connected(&id);
        async move {
            if connected {
                return Ok(None);
            }

            let channel = match proxy
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
                    warn!("FIDL error on connect: {:?}", e);
                    return Err(e.into());
                }
                Ok(Err(e)) => {
                    warn!("Couldn't connect to {}: {:?}", id, e);
                    return Err(format_err!("Bluetooth error: {:?}", e));
                }
                Ok(Ok(channel)) => channel,
            };

            info!("{} got channel back when connecting..", id);

            let channel = channel
                .try_into()
                .map_err(|e| format_err!("Couldn't convert fidl to BT channel: {:?}", e))?;
            Ok(Some(channel))
        }
    }

    /// Accept a channel that was connected to the peer `id`.
    /// If `initiator_delay` is set, attempt to start a stream after the specified delay.
    /// `initiator_delay` has no effect if the peer already has a control channel.
    /// Returns a weak peer pointer (even if it was previously connected) if successful.
    pub fn connected(
        &mut self,
        id: PeerId,
        channel: Channel,
        initiator_delay: Option<zx::Duration>,
    ) -> Result<DetachableWeak<PeerId, Peer>, Error> {
        if let Some(weak) = self.get_weak(&id) {
            let peer = weak.upgrade().ok_or(format_err!("Disconnected connecting transport"))?;
            info!("{} Connecting transport channel..", id);
            if let Err(e) = peer.receive_channel(channel) {
                warn!("{} failed to connect channel: {}", id, e);
                return Err(e.into());
            }
            return Ok(weak);
        }

        let entry = self.connected.lazy_entry(&id);

        info!("Adding new peer {}", id);
        let avdtp_peer = avdtp::Peer::new(channel);

        let mut peer = Peer::create(
            id,
            avdtp_peer,
            self.streams.as_new(),
            Some(self.permits.clone()),
            self.profile.clone(),
            self.cobalt_sender.clone(),
        );

        self.discovered.connected(id);

        let peer_preferred_direction = if let Some((desc, dir)) = self.discovered.get(&id) {
            peer.set_descriptor(desc);
            dir
        } else {
            None
        };

        if let Err(e) = peer.iattach(&self.inspect, inspect::unique_name("peer_")) {
            warn!("Couldn't attach peer {} to inspect tree: {:?}", id, e);
        }

        let closed_fut = peer.closed();
        let peer = match entry.try_insert(peer) {
            Err(_peer) => {
                warn!("Peer connected while we were setting up peer: {}", id);
                return self.get_weak(&id).ok_or(format_err!("Peer missing"));
            }
            Ok(weak_peer) => weak_peer,
        };

        if let Some(delay) = initiator_delay {
            let peer = peer.clone();
            let peer_id = peer.key().clone();
            let mut negotiation = self.codec_negotiation.clone();
            // Bias the codec negotiation with the peer's preferred direction that was discovered
            // from the SDP service search.
            if let Some(dir) = peer_preferred_direction {
                negotiation.set_direction(dir);
            }
            let start_stream_task = fuchsia_async::Task::local(async move {
                info!(
                    "Peer {}: dwelling {}s for peer initiation",
                    peer.key(),
                    delay.into_millis() as f64 / 1000.0
                );
                fasync::Timer::new(fasync::Time::after(delay)).await;

                if let Err(e) = ConnectedPeers::start_streaming(&peer, negotiation).await {
                    info!("Peer {} start failed with error: {:?}", peer.key(), e);
                    peer.detach();
                }
            });
            self.start_stream_tasks.insert(peer_id, start_stream_task);
        }

        // Remove the peer when we disconnect.
        fasync::Task::local(async move {
            closed_fut.await;
            peer.detach();
        })
        .detach();

        let peer = self.get_weak(&id).ok_or(format_err!("Peer missing"))?;
        self.notify_connected(&peer);
        Ok(peer)
    }

    /// Notify the listeners that a new peer has been connected to.
    fn notify_connected(&mut self, peer: &DetachableWeak<PeerId, Peer>) {
        let mut i = 0;
        while i != self.connected_peer_senders.len() {
            if let Err(_) = self.connected_peer_senders[i].try_send(peer.clone()) {
                let _ = self.connected_peer_senders.swap_remove(i);
            } else {
                i += 1;
            }
        }
    }

    /// Get a stream that produces peers that have been connected.
    pub fn connected_stream(&mut self) -> PeerConnections {
        let (sender, receiver) = mpsc::channel(0);
        self.connected_peer_senders.push(sender);
        PeerConnections { stream: receiver }
    }
}

impl Inspect for &mut ConnectedPeers {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name.as_ref());
        let peer_dir_str = format!("{:?}", self.preferred_direction());
        self.inspect_peer_direction =
            self.inspect.create_string("preferred_peer_direction", peer_dir_str);
        self.streams.iattach(&self.inspect, "local_streams")?;
        self.discovered.iattach(&self.inspect, "discovered")
    }
}

/// Provides a stream of peers that have been connected to. This stream produces an item whenever
/// an A2DP peer has been connected.  It will produce None when no more peers will be connected.
pub struct PeerConnections {
    stream: mpsc::Receiver<DetachableWeak<PeerId, Peer>>,
}

impl Stream for PeerConnections {
    type Item = DetachableWeak<PeerId, Peer>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.stream.poll_next_unpin(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use bt_avdtp::{Request, ServiceCapability};
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::{
        ProfileMarker, ProfileRequestStream, ServiceClassProfileIdentifier,
    };
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fuchsia_inspect::assert_data_tree;
    use futures::channel::mpsc;
    use futures::{self, pin_mut, task::Poll, StreamExt};
    use std::{
        convert::{TryFrom, TryInto},
        iter::FromIterator,
    };

    use crate::{media_task::tests::TestMediaTaskBuilder, media_types::*, stream::Stream};

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn run_to_stalled(exec: &mut fasync::TestExecutor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    fn exercise_avdtp(exec: &mut fasync::TestExecutor, remote: Channel, peer: &Peer) {
        let remote_avdtp = avdtp::Peer::new(remote);
        let mut remote_requests = remote_avdtp.take_request_stream();

        // Should be able to actually communicate via the peer.
        let avdtp = peer.avdtp();
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
    ) -> (fasync::TestExecutor, PeerId, ConnectedPeers, ProfileRequestStream) {
        let exec = fasync::TestExecutor::new().expect("executor should build");
        let (proxy, stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let id = PeerId(1);
        let (cobalt_sender, _) = fake_cobalt_sender();

        let peers = ConnectedPeers::new(
            Streams::new(),
            CodecNegotiation::build(vec![], avdtp::EndpointType::Sink).unwrap(),
            Permits::new(1),
            proxy,
            Some(cobalt_sender),
        );

        (exec, id, peers, stream)
    }

    #[test]
    fn connect_creates_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();

        let peer = peers.connected(id, channel, None).expect("peer should connect");
        let peer = peer.upgrade().expect("peer should be connected");

        exercise_avdtp(&mut exec, remote, &peer);
    }

    #[test]
    fn connect_notifies_streams() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();

        let mut peer_stream = peers.connected_stream();
        let mut peer_stream_two = peers.connected_stream();

        let peer = peers.connected(id, channel, None).expect("peer should connect");
        let peer = peer.upgrade().expect("peer should be connected");

        // Peers should have been notified of the new peer
        let weak = exec.run_singlethreaded(peer_stream.next()).expect("peer stream to produce");
        assert_eq!(weak.key(), &id);
        let weak = exec.run_singlethreaded(peer_stream_two.next()).expect("peer stream to produce");
        assert_eq!(weak.key(), &id);

        exercise_avdtp(&mut exec, remote, &peer);

        // If you drop one stream, the other one should still produce.
        drop(peer_stream);

        let id2 = PeerId(2);
        let (remote2, channel2) = Channel::create();
        let peer2 = peers.connected(id2, channel2, None).expect("peer should connect");
        let peer2 = peer2.upgrade().expect("peer two should be connected");

        let weak = exec.run_singlethreaded(peer_stream_two.next()).expect("peer stream to produce");
        assert_eq!(weak.key(), &id2);

        exercise_avdtp(&mut exec, remote2, &peer2);
    }

    #[test]
    fn find_preferred_direction_returns_correct_endpoints() {
        let empty = HashSet::new();
        assert_eq!(find_preferred_direction(&empty), None);

        let sink_only = HashSet::from_iter(vec![avdtp::EndpointType::Sink].into_iter());
        assert_eq!(find_preferred_direction(&sink_only), Some(avdtp::EndpointType::Sink));

        let source_only = HashSet::from_iter(vec![avdtp::EndpointType::Source].into_iter());
        assert_eq!(find_preferred_direction(&source_only), Some(avdtp::EndpointType::Source));

        let both = HashSet::from_iter(
            vec![avdtp::EndpointType::Sink, avdtp::EndpointType::Source].into_iter(),
        );
        assert_eq!(find_preferred_direction(&both), None);
    }

    // Arbitrarily chosen ID for the SBC sink stream endpoint.
    const SBC_SINK_SEID: u8 = 9;

    // Arbitrarily chosen ID for the AAC stream endpoint.
    const AAC_SEID: u8 = 10;

    // Arbitrarily chosen ID for the SBC source stream endpoint.
    const SBC_SOURCE_SEID: u8 = 11;

    fn build_test_stream(
        id: u8,
        type_: avdtp::EndpointType,
        codec_cap: avdtp::ServiceCapability,
    ) -> Stream {
        let endpoint = avdtp::StreamEndpoint::new(
            id,
            avdtp::MediaType::Audio,
            type_,
            vec![avdtp::ServiceCapability::MediaTransport, codec_cap],
        )
        .expect("endpoint builds");
        let task_builder = TestMediaTaskBuilder::new();

        Stream::build(endpoint, task_builder.builder())
    }

    fn aac_sink_codec() -> avdtp::ServiceCapability {
        AacCodecInfo::new(
            AacObjectType::MANDATORY_SNK,
            AacSamplingFrequency::MANDATORY_SNK,
            AacChannels::MANDATORY_SNK,
            true,
            0, // 0 = Unknown constant bitrate support (A2DP Sec. 4.5.2.4)
        )
        .unwrap()
        .into()
    }

    fn sbc_sink_codec() -> avdtp::ServiceCapability {
        SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .unwrap()
        .into()
    }

    fn sbc_source_codec() -> avdtp::ServiceCapability {
        SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .unwrap()
        .into()
    }

    /// Sets up a test in which we expect to select a stream and connect to a peer.
    /// Returns the executor, connected peers (under test), request stream for profile interaction,
    /// and an SBC and AAC Sink service capability.
    fn setup_negotiation_test() -> (
        fasync::TestExecutor,
        ConnectedPeers,
        ProfileRequestStream,
        ServiceCapability,
        ServiceCapability,
    ) {
        let exec = fasync::TestExecutor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000));
        let (proxy, stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();

        let aac_sink_codec: avdtp::ServiceCapability = aac_sink_codec();
        let sbc_sink_codec: avdtp::ServiceCapability = sbc_sink_codec();
        let sbc_source_codec: avdtp::ServiceCapability = sbc_source_codec();

        // Set up the codec negotiation with a default preferred direction of source.
        let negotiation = CodecNegotiation::build(
            vec![aac_sink_codec.clone(), sbc_sink_codec.clone(), sbc_source_codec.clone()],
            avdtp::EndpointType::Source,
        )
        .unwrap();

        let mut streams = Streams::new();
        streams.insert(build_test_stream(
            SBC_SINK_SEID,
            avdtp::EndpointType::Sink,
            sbc_sink_codec.clone(),
        ));
        streams.insert(build_test_stream(
            AAC_SEID,
            avdtp::EndpointType::Sink,
            aac_sink_codec.clone(),
        ));
        streams.insert(build_test_stream(
            SBC_SOURCE_SEID,
            avdtp::EndpointType::Source,
            sbc_source_codec.clone(),
        ));

        let peers = ConnectedPeers::new(
            streams,
            negotiation.clone(),
            Permits::new(1),
            proxy,
            Some(cobalt_sender),
        );

        (exec, peers, stream, sbc_sink_codec, aac_sink_codec)
    }

    #[test]
    fn streaming_start_with_streaming_peer_is_noop() {
        let (mut exec, mut peers, _stream, sbc_codec, _aac_codec) = setup_negotiation_test();
        let id = PeerId(1);
        let (remote, channel) = Channel::create();
        let remote = avdtp::Peer::new(remote);

        let delay = zx::Duration::from_seconds(1);

        let mut remote_requests = remote.take_request_stream();

        // This starts the task in the background waiting.
        assert!(peers.connected(id, channel, Some(delay)).is_ok());
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Before the delay expires, the peer starts the stream.

        let seid: avdtp::StreamEndpointId = SBC_SINK_SEID.try_into().expect("seid to be okay");
        let config_caps = &[ServiceCapability::MediaTransport, sbc_codec];
        let set_config_fut = remote.set_configuration(&seid, &seid, config_caps);
        pin_mut!(set_config_fut);
        match exec.run_until_stalled(&mut set_config_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected set config to be ready and Ok, got {:?}", x),
        };

        // The remote peer doesn't need to actually open, Set Configuration is enough of a signal.
        // wait for the delay to expire now.

        exec.set_fake_time(fasync::Time::after(delay) + zx::Duration::from_micros(1));
        exec.wake_expired_timers();

        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Shouldn't start a discovery, since the stream is scheduled to start already.
        assert!(exec.run_until_stalled(&mut remote_requests.next()).is_pending());
    }

    fn sbc_source_endpoint() -> (avdtp::StreamEndpointId, avdtp::StreamInformation) {
        let remote_sbc_seid: avdtp::StreamEndpointId = 1u8.try_into().unwrap();
        let info = avdtp::StreamInformation::new(
            remote_sbc_seid.clone(),
            false,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Source,
        );
        (remote_sbc_seid, info)
    }

    fn aac_source_endpoint() -> (avdtp::StreamEndpointId, avdtp::StreamInformation) {
        let remote_aac_seid: avdtp::StreamEndpointId = 2u8.try_into().unwrap();
        let info = avdtp::StreamInformation::new(
            remote_aac_seid.clone(),
            false,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Source,
        );
        (remote_aac_seid, info)
    }

    fn sbc_sink_endpoint() -> (avdtp::StreamEndpointId, avdtp::StreamInformation) {
        let remote_sbc_seid: avdtp::StreamEndpointId = 3u8.try_into().unwrap();
        let info = avdtp::StreamInformation::new(
            remote_sbc_seid.clone(),
            false,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
        );
        (remote_sbc_seid, info)
    }

    /// Expects an AVDTP Discovery request on the `requests` stream. Responds to
    /// the request with the provided `response` endpoints.
    fn expect_peer_discovery(
        exec: &mut fasync::TestExecutor,
        requests: &mut avdtp::RequestStream,
        response: Vec<avdtp::StreamInformation>,
    ) {
        match exec.run_until_stalled(&mut requests.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::Discover { responder }))) => {
                responder.send(&response).expect("response succeeds");
            }
            x => panic!("Expected a discovery request to be sent after delay, got {:?}", x),
        };
    }

    #[test]
    fn streaming_start_configure_while_discovery() {
        let (mut exec, mut peers, _stream, sbc_codec, _aac_codec) = setup_negotiation_test();
        let id = PeerId(1);
        let (remote, channel) = Channel::create();
        let remote = avdtp::Peer::new(remote);

        let delay = zx::Duration::from_seconds(1);

        let mut remote_requests = remote.take_request_stream();

        // This starts the task in the background waiting.
        assert!(peers.connected(id, channel, Some(delay)).is_ok());
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // The delay expires, and the discovery is start!
        exec.set_fake_time(fasync::Time::after(delay) + zx::Duration::from_micros(1));
        exec.wake_expired_timers();
        expect_peer_discovery(
            &mut exec,
            &mut remote_requests,
            vec![sbc_source_endpoint().1, aac_source_endpoint().1],
        );

        // The remote peer doesn't need to actually open, Set Configuration is enough of a signal.
        let seid: avdtp::StreamEndpointId = SBC_SINK_SEID.try_into().expect("seid to be okay");
        let config_caps = &[ServiceCapability::MediaTransport, sbc_codec.clone()];
        let set_config_fut = remote.set_configuration(&seid, &seid, config_caps);
        pin_mut!(set_config_fut);
        match exec.run_until_stalled(&mut set_config_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected set config to be ready and Ok, got {:?}", x),
        };

        // Can finish the collection process, but not attempt to configure or start a stream.
        loop {
            match exec.run_until_stalled(&mut remote_requests.next()) {
                Poll::Ready(Some(Ok(avdtp::Request::GetCapabilities { responder, .. }))) => {
                    responder
                        .send(&vec![avdtp::ServiceCapability::MediaTransport, sbc_codec.clone()])
                        .expect("respond succeeds");
                }
                Poll::Ready(x) => panic!("Got unexpected request: {:?}", x),
                Poll::Pending => break,
            }
        }
    }

    /// Tests connection initiation selects the appropriate stream endpoint based
    /// on a biased codec negotiation that is set from the peer's discovered services.
    #[test]
    fn connect_initiation_uses_biased_codec_negotiation_by_peer() {
        let (mut exec, mut peers, _stream, sbc_codec, _aac_codec) = setup_negotiation_test();
        let id = PeerId(1);
        let (remote, channel) = Channel::create();

        // System biases towards the Source direction (called when the AudioMode FIDL changes).
        peers.set_preferred_direction(avdtp::EndpointType::Source);

        // New fake peer discovered with some descriptor - the peer's SDP entry shows Sink.
        let remote = avdtp::Peer::new(remote);
        let desc = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        };
        let preferred_direction = vec![avdtp::EndpointType::Sink];
        let delay = zx::Duration::from_seconds(1);
        peers.found(id, desc, HashSet::from_iter(preferred_direction.into_iter()));

        peers.connected(id, channel, Some(delay)).expect("connect control channel is ok");
        // run the start task until it's stalled.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        let mut remote_requests = remote.take_request_stream();

        // Should wait for the specified amount of time.
        assert!(exec.run_until_stalled(&mut remote_requests.next()).is_pending());

        exec.set_fake_time(fasync::Time::after(delay + zx::Duration::from_micros(1)));
        exec.wake_expired_timers();

        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        // Even though the peer supports both SBC Sink and Source, we expect to negotiate and start
        // on the Sink endpoint since that is the peer's preferred one.
        let (peer_sbc_source_seid, peer_sbc_source_endpoint) = sbc_source_endpoint();
        let (peer_sbc_sink_seid, peer_sbc_sink_endpoint) = sbc_sink_endpoint();
        expect_peer_discovery(
            &mut exec,
            &mut remote_requests,
            vec![peer_sbc_source_endpoint, peer_sbc_sink_endpoint],
        );
        for _twice in 1..=2 {
            match exec.run_until_stalled(&mut remote_requests.next()) {
                Poll::Ready(Some(Ok(avdtp::Request::GetCapabilities { stream_id, responder }))) => {
                    let codec = match stream_id {
                        id if id == peer_sbc_source_seid => sbc_codec.clone(),
                        id if id == peer_sbc_sink_seid => sbc_codec.clone(),
                        x => panic!("Got unexpected get_capabilities seid {:?}", x),
                    };
                    responder
                        .send(&vec![avdtp::ServiceCapability::MediaTransport, codec])
                        .expect("respond succeeds");
                }
                x => panic!("Expected a ready get capabilities request, got {:?}", x),
            };
        }

        match exec.run_until_stalled(&mut remote_requests.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::SetConfiguration {
                local_stream_id,
                remote_stream_id,
                capabilities: _,
                responder,
            }))) => {
                // We expect the set configuration to apply to the remote peer's Sink SEID and the
                // local Source SEID.
                assert_eq!(peer_sbc_sink_seid, local_stream_id);
                let local_sbc_source_seid: avdtp::StreamEndpointId =
                    SBC_SOURCE_SEID.try_into().unwrap();
                assert_eq!(local_sbc_source_seid, remote_stream_id);
                responder.send().expect("response sends");
            }
            x => panic!("Expected a ready set configuration request, got {:?}", x),
        };
    }

    /// Tests connection initiation selects the appropriate stream endpoint based
    /// on a biased codec negotiation that is set from by the system (in practice, the AudioMode
    /// FIDL). This case typically occurs when a peer advertises both sink and source, and therefore
    /// has no preference for the endpoint direction.
    #[test]
    fn connect_initiation_uses_biased_codec_negotiation_by_system() {
        let (mut exec, mut peers, _stream, sbc_codec, _aac_codec) = setup_negotiation_test();
        let id = PeerId(1);
        let (remote, channel) = Channel::create();

        // System biases towards the Source direction (called when the AudioMode FIDL changes).
        peers.set_preferred_direction(avdtp::EndpointType::Source);

        // New fake peer discovered with separate Sink and Source entries.
        let remote = avdtp::Peer::new(remote);
        let desc = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        };
        peers.found(
            id,
            desc.clone(),
            HashSet::from_iter(vec![avdtp::EndpointType::Source].into_iter()),
        );
        peers.found(id, desc, HashSet::from_iter(vec![avdtp::EndpointType::Sink].into_iter()));

        let delay = zx::Duration::from_seconds(1);
        peers.connected(id, channel, Some(delay)).expect("connect control channel is ok");
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        let mut remote_requests = remote.take_request_stream();
        // Should wait for the specified amount of time.
        assert!(exec.run_until_stalled(&mut remote_requests.next()).is_pending());
        exec.set_fake_time(fasync::Time::after(delay + zx::Duration::from_micros(1)));
        exec.wake_expired_timers();
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Because the peer advertises both Sink and Source, we fall back to the system-biased
        // direction, which is Source for the peer.
        let (peer_sbc_source_seid, peer_sbc_source_endpoint) = sbc_source_endpoint();
        let (peer_sbc_sink_seid, peer_sbc_sink_endpoint) = sbc_sink_endpoint();
        expect_peer_discovery(
            &mut exec,
            &mut remote_requests,
            vec![peer_sbc_source_endpoint, peer_sbc_sink_endpoint],
        );
        for _twice in 1..=2 {
            match exec.run_until_stalled(&mut remote_requests.next()) {
                Poll::Ready(Some(Ok(avdtp::Request::GetCapabilities { stream_id, responder }))) => {
                    let codec = match stream_id {
                        id if id == peer_sbc_source_seid => sbc_codec.clone(),
                        id if id == peer_sbc_sink_seid => sbc_codec.clone(),
                        x => panic!("Got unexpected get_capabilities seid {:?}", x),
                    };
                    responder
                        .send(&vec![avdtp::ServiceCapability::MediaTransport, codec])
                        .expect("respond succeeds");
                }
                x => panic!("Expected a ready get capabilities request, got {:?}", x),
            };
        }

        match exec.run_until_stalled(&mut remote_requests.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::SetConfiguration {
                local_stream_id,
                remote_stream_id,
                capabilities: _,
                responder,
            }))) => {
                // We expect the set configuration to apply to the remote peer's Source SEID and the
                // local Sink SEID.
                assert_eq!(peer_sbc_source_seid, local_stream_id);
                let local_sbc_sink_seid: avdtp::StreamEndpointId =
                    SBC_SINK_SEID.try_into().unwrap();
                assert_eq!(local_sbc_sink_seid, remote_stream_id);
                responder.send().expect("response sends");
            }
            x => panic!("Expected a ready set configuration request, got {:?}", x),
        };
    }

    #[test]
    fn connect_initiation_uses_negotiation() {
        let (mut exec, mut peers, _stream, sbc_codec, aac_codec) = setup_negotiation_test();
        let id = PeerId(1);
        let (remote, channel) = Channel::create();
        let remote = avdtp::Peer::new(remote);

        let delay = zx::Duration::from_seconds(1);

        peers.connected(id, channel, Some(delay)).expect("connect control channel is ok");

        // run the start task until it's stalled.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        let mut remote_requests = remote.take_request_stream();

        // Should wait for the specified amount of time.
        assert!(exec.run_until_stalled(&mut remote_requests.next()).is_pending());

        exec.set_fake_time(fasync::Time::after(delay + zx::Duration::from_micros(1)));
        exec.wake_expired_timers();

        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // Should discover remote streams, negotiate, and start.
        let (peer_sbc_seid, peer_sbc_endpoint) = sbc_source_endpoint();
        let (peer_aac_seid, peer_aac_endpoint) = aac_source_endpoint();
        expect_peer_discovery(
            &mut exec,
            &mut remote_requests,
            vec![peer_sbc_endpoint, peer_aac_endpoint],
        );
        for _twice in 1..=2 {
            match exec.run_until_stalled(&mut remote_requests.next()) {
                Poll::Ready(Some(Ok(avdtp::Request::GetCapabilities { stream_id, responder }))) => {
                    let codec = match stream_id {
                        id if id == peer_sbc_seid => sbc_codec.clone(),
                        id if id == peer_aac_seid => aac_codec.clone(),
                        x => panic!("Got unexpected get_capabilities seid {:?}", x),
                    };
                    responder
                        .send(&vec![avdtp::ServiceCapability::MediaTransport, codec])
                        .expect("respond succeeds");
                }
                x => panic!("Expected a ready get capabilities request, got {:?}", x),
            };
        }

        match exec.run_until_stalled(&mut remote_requests.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::SetConfiguration {
                local_stream_id,
                remote_stream_id,
                capabilities: _,
                responder,
            }))) => {
                // Should set the aac stream, matched with local AAC seid.
                assert_eq!(peer_aac_seid, local_stream_id);
                let local_aac_seid: avdtp::StreamEndpointId = AAC_SEID.try_into().unwrap();
                assert_eq!(local_aac_seid, remote_stream_id);
                responder.send().expect("response sends");
            }
            x => panic!("Expected a ready set configuration request, got {:?}", x),
        };
    }

    #[test]
    fn connected_peers_inspect() {
        let (_exec, id, mut peers, _stream) = setup_connected_peer_test();

        let inspect = inspect::Inspector::new();
        peers.iattach(inspect.root(), "peers").expect("should attach to inspect tree");

        assert_data_tree!(inspect, root: {
            peers: { local_streams: contains {}, discovered: contains {}, preferred_peer_direction: "Sink" }});

        peers.set_preferred_direction(avdtp::EndpointType::Source);

        assert_data_tree!(inspect, root: {
            peers: { local_streams: contains {}, discovered: contains {}, preferred_peer_direction: "Source" }});

        // Connect a peer, it should show up in the tree.
        let (_remote, channel) = Channel::create();
        assert!(peers.connected(id, channel, None).is_ok());

        assert_data_tree!(inspect, root: {
            peers: {
                discovered: contains {},
                preferred_peer_direction: "Source",
                local_streams: contains {},
                peer_0: { id: "0000000000000001", local_streams: contains {} }
            }
        });
    }

    #[test]
    fn connected_peers_peer_disconnect_removes_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();

        assert!(peers.connected(id, channel, None).is_ok());
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());
    }

    #[test]
    fn connected_peers_reconnect_works() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();
        assert!(peers.connected(id, channel, None).is_ok());
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());

        // Connect another peer with the same ID
        let (_remote, channel) = Channel::create();

        assert!(peers.connected(id, channel, None).is_ok());
        run_to_stalled(&mut exec);

        // Should be connected.
        assert!(peers.get(&id).is_some());
    }
}
