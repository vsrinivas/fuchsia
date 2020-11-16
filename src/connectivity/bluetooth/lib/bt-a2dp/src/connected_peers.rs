// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_avdtp as avdtp,
    fidl_fuchsia_bluetooth_bredr::{ProfileDescriptor, ProfileProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak},
        types::{Channel, PeerId},
    },
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
    log::{info, warn},
    std::{collections::HashMap, sync::Arc},
};

// TODO(fxbug.dev/54334): Cleanup Peer Session/Stream generation.
pub type PeerSessionFn =
    dyn Fn(&PeerId) -> fasync::Task<Result<(Streams, CodecNegotiation, fasync::Task<()>), Error>>;

use crate::{codec::CodecNegotiation, peer::Peer, stream::Streams};

/// ConnectedPeers manages the set of connected peers based on discovery, new connection, and
/// peer session lifetime.
pub struct ConnectedPeers {
    /// The set of connected peers.
    connected: DetachableMap<PeerId, Peer>,
    /// ProfileDescriptors from discovering the peer, stored here before a peer connects.
    descriptors: HashMap<PeerId, ProfileDescriptor>,
    /// A generator which returns an appropriate set of streams for a peer given it's id.
    peer_session_gen: Box<PeerSessionFn>,
    /// Profile Proxy, used to connect new transport sockets.
    profile: ProfileProxy,
    /// Cobalt logger to use and hand out to peers, if we are using one.
    cobalt_sender: Option<CobaltSender>,
    /// The 'peers' node of the inspect tree. All connected peers own a child node of this node.
    inspect: inspect::Node,
}

impl ConnectedPeers {
    pub fn new(
        peer_session_gen: Box<PeerSessionFn>,
        profile: ProfileProxy,
        cobalt_sender: Option<CobaltSender>,
    ) -> Self {
        Self {
            connected: DetachableMap::new(),
            descriptors: HashMap::new(),
            peer_session_gen,
            profile,
            inspect: inspect::Node::default(),
            cobalt_sender,
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

    async fn start_streaming(
        peer: &DetachableWeak<PeerId, Peer>,
        negotiation: CodecNegotiation,
    ) -> Result<(), anyhow::Error> {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        let remote_streams = strong.collect_capabilities().await?;

        let (negotiated, remote_seid) =
            negotiation.select(&remote_streams).ok_or(format_err!("No compatible stream found"))?;

        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.stream_start(remote_seid, negotiated).await.map_err(Into::into)
    }

    pub fn found(&mut self, id: PeerId, desc: ProfileDescriptor) {
        self.descriptors.insert(id, desc.clone());
        self.get(&id).map(|p| p.set_descriptor(desc));
    }

    /// Accept a channel that was connected to the peer `id`.  If `initiator` is true, we initiated
    /// this connection (and should take the INT role)
    /// Returns a weak peer pointer if connected (even if it was connected before) if successful.
    pub async fn connected(
        &mut self,
        id: PeerId,
        channel: Channel,
        initiator: bool,
    ) -> Result<DetachableWeak<PeerId, Peer>, Error> {
        if let Some(weak) = self.get_weak(&id) {
            let peer = weak.upgrade().ok_or(format_err!("Disconnected connecting transport"))?;
            if let Err(e) = peer.receive_channel(channel) {
                warn!("{} failed to connect channel: {}", id, e);
                return Err(e.into());
            }
            return Ok(weak);
        }

        let (streams, negotiation, session_task) = match (self.peer_session_gen)(&id).await {
            Ok(x) => x,
            Err(e) => {
                warn!("Couldn't generate peer session for {}: {:?}", id, e);
                return Err(format_err!("Couldn't generate peer session"));
            }
        };

        let entry = self.connected.lazy_entry(&id);

        info!("Adding new peer for {}", id);
        let avdtp_peer = avdtp::Peer::new(channel);

        let mut peer =
            Peer::create(id, avdtp_peer, streams, self.profile.clone(), self.cobalt_sender.clone());

        if let Some(desc) = self.descriptors.get(&id) {
            peer.set_descriptor(desc.clone());
        }

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

        if initiator {
            let peer_clone = peer.clone();
            fuchsia_async::Task::local(async move {
                if let Err(e) = ConnectedPeers::start_streaming(&peer_clone, negotiation).await {
                    info!("Peer {} start failed with error: {:?}", peer_clone.key(), e);
                    peer_clone.detach();
                }
            })
            .detach();
        }

        // Remove the peer when we disconnect.
        fasync::Task::local(async move {
            closed_fut.await;
            peer.detach();
            // Captures the session task to extend the task lifetime.
            drop(session_task);
        })
        .detach();
        self.get_weak(&id).ok_or(format_err!("Peer missing"))
    }
}

impl Inspect for &mut ConnectedPeers {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use bt_avdtp::Request;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequestStream};
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fuchsia_inspect::assert_inspect_tree;
    use futures::channel::mpsc;
    use futures::{self, task::Poll, StreamExt};
    use std::convert::{TryFrom, TryInto};

    use crate::{media_task::tests::TestMediaTaskBuilder, media_types::*, stream::Stream};

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn run_to_stalled(exec: &mut fasync::Executor) {
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
    }

    fn exercise_avdtp(exec: &mut fasync::Executor, remote: Channel, peer: &Peer) {
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

    fn no_streams_session_fn() -> Box<PeerSessionFn> {
        Box::new(|_peer_id| {
            fasync::Task::spawn(async {
                Ok((
                    Streams::new(),
                    CodecNegotiation::build(vec![]).unwrap(),
                    fasync::Task::spawn(async {}),
                ))
            })
        })
    }

    fn setup_connected_peer_test(
    ) -> (fasync::Executor, PeerId, ConnectedPeers, ProfileRequestStream) {
        let exec = fasync::Executor::new().expect("executor should build");
        let (proxy, stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let id = PeerId(1);
        let (cobalt_sender, _) = fake_cobalt_sender();

        let peers = ConnectedPeers::new(no_streams_session_fn(), proxy, Some(cobalt_sender));

        (exec, id, peers, stream)
    }

    #[test]
    fn connect_creates_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();

        let peer = exec
            .run_singlethreaded(peers.connected(id, channel, false))
            .expect("peer should connect");
        let peer = peer.upgrade().expect("peer should be connected");

        exercise_avdtp(&mut exec, remote, &peer);
    }

    // Arbitrarily chosen ID for the SBC stream endpoint.
    const SBC_SEID: u8 = 9;

    // Arbitrarily chosen ID for the AAC stream endpoint.
    const AAC_SEID: u8 = 10;

    fn build_test_stream(id: u8, codec_cap: avdtp::ServiceCapability) -> Stream {
        let endpoint = avdtp::StreamEndpoint::new(
            id,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![avdtp::ServiceCapability::MediaTransport, codec_cap],
        )
        .expect("endpoint builds");
        let task_builder = TestMediaTaskBuilder::new();

        Stream::build(endpoint, task_builder.builder())
    }

    #[test]
    fn connect_initiation_uses_negotiation() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, _stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let id = PeerId(1);
        let (cobalt_sender, _) = fake_cobalt_sender();

        let (remote, channel) = Channel::create();
        let remote = avdtp::Peer::new(remote);

        let aac_codec: avdtp::ServiceCapability = AacCodecInfo::new(
            AacObjectType::MANDATORY_SNK,
            AacSamplingFrequency::MANDATORY_SNK,
            AacChannels::MANDATORY_SNK,
            true,
            0, // 0 = Unknown constant bitrate support (A2DP Sec. 4.5.2.4)
        )
        .unwrap()
        .into();
        let remote_aac_seid: avdtp::StreamEndpointId = 2u8.try_into().unwrap();

        let sbc_codec: avdtp::ServiceCapability = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .unwrap()
        .into();
        let remote_sbc_seid: avdtp::StreamEndpointId = 1u8.try_into().unwrap();

        let negotiation =
            CodecNegotiation::build(vec![aac_codec.clone(), sbc_codec.clone()]).unwrap();

        let mut streams = Streams::new();
        streams.insert(build_test_stream(SBC_SEID, sbc_codec.clone()));
        streams.insert(build_test_stream(AAC_SEID, aac_codec.clone()));

        let session_fn: Box<PeerSessionFn> = Box::new(move |_peer_id| {
            let negotiation_clone = negotiation.clone();
            let new_streams = streams.as_new();
            fasync::Task::spawn(async move {
                Ok((new_streams, negotiation_clone, fasync::Task::spawn(async {})))
            })
        });

        let mut peers = ConnectedPeers::new(session_fn, proxy, Some(cobalt_sender));

        assert!(exec.run_singlethreaded(peers.connected(id, channel, true)).is_ok());

        // Should discover remote streams, negotiate, and start.

        let mut remote_requests = remote.take_request_stream();

        match exec.run_singlethreaded(&mut remote_requests.next()) {
            Some(Ok(avdtp::Request::Discover { responder })) => {
                let endpoints = vec![
                    avdtp::StreamInformation::new(
                        remote_sbc_seid.clone(),
                        false,
                        avdtp::MediaType::Audio,
                        avdtp::EndpointType::Source,
                    ),
                    avdtp::StreamInformation::new(
                        remote_aac_seid.clone(),
                        false,
                        avdtp::MediaType::Audio,
                        avdtp::EndpointType::Source,
                    ),
                ];
                responder.send(&endpoints).expect("response succeeds");
            }
            x => panic!("Expected a discovery request, got {:?}", x),
        };

        for _twice in 1..=2 {
            match exec.run_singlethreaded(&mut remote_requests.next()) {
                Some(Ok(avdtp::Request::GetCapabilities { stream_id, responder })) => {
                    if stream_id == remote_sbc_seid {
                        responder.send(&vec![
                            avdtp::ServiceCapability::MediaTransport,
                            sbc_codec.clone(),
                        ])
                    } else if stream_id == remote_aac_seid {
                        responder.send(&vec![
                            avdtp::ServiceCapability::MediaTransport,
                            aac_codec.clone(),
                        ])
                    } else {
                        responder.reject(avdtp::ErrorCode::BadAcpSeid)
                    }
                    .expect("respond succeeds");
                }
                x => panic!("Expected a get capabilities request, got {:?}", x),
            };
        }

        match exec.run_singlethreaded(&mut remote_requests.next()) {
            Some(Ok(avdtp::Request::SetConfiguration {
                local_stream_id,
                remote_stream_id,
                capabilities: _,
                responder,
            })) => {
                // Should set the aac stream, matched with local AAC seid.
                assert_eq!(remote_aac_seid, local_stream_id);
                let local_aac_seid: avdtp::StreamEndpointId = AAC_SEID.try_into().unwrap();
                assert_eq!(local_aac_seid, remote_stream_id);
                responder.send().expect("response sends");
            }
            x => panic!("Expected a set configuration request, got {:?}", x),
        };
    }

    #[test]
    fn connected_peers_inspect() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let inspect = inspect::Inspector::new();
        peers.iattach(inspect.root(), "peers").expect("should attach to inspect tree");

        assert_inspect_tree!(inspect, root: { peers: {} });

        // Connect a peer, it should show up in the tree.
        let (_remote, channel) = Channel::create();
        assert!(exec.run_singlethreaded(peers.connected(id, channel, false)).is_ok());

        assert_inspect_tree!(inspect, root: { peers: { peer_0: {
            id: "0000000000000001", local_streams: contains {}
        }}});
    }

    #[test]
    fn connected_peers_peer_disconnect_removes_peer() {
        let (mut exec, id, mut peers, _stream) = setup_connected_peer_test();

        let (remote, channel) = Channel::create();

        assert!(exec.run_singlethreaded(peers.connected(id, channel, false)).is_ok());
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
        assert!(exec.run_singlethreaded(peers.connected(id, channel, false)).is_ok());
        run_to_stalled(&mut exec);

        // Disconnect the signaling channel, peer should be gone.
        drop(remote);

        run_to_stalled(&mut exec);

        assert!(peers.get(&id).is_none());

        // Connect another peer with the same ID
        let (_remote, channel) = Channel::create();

        assert!(exec.run_singlethreaded(peers.connected(id, channel, false)).is_ok());
        run_to_stalled(&mut exec);

        // Should be connected.
        assert!(peers.get(&id).is_some());
    }
}
