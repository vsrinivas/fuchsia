// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_a2dp::{codec::CodecNegotiation, peer::ControllerPool, peer::Peer, stream},
    bt_avdtp as avdtp,
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak, LazyEntry},
        types::{Channel, PeerId},
    },
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
    fuchsia_zircon as zx,
    log::{info, warn},
    std::{convert::TryInto, sync::Arc},
};

pub struct Peers {
    peers: DetachableMap<PeerId, Peer>,
    streams: stream::Streams,
    negotiation: CodecNegotiation,
    profile: bredr::ProfileProxy,
    /// The L2CAP channel mode preference for the AVDTP signaling channel.
    channel_mode: bredr::ChannelMode,
    /// The controller used for sending out-of-band commands.
    controller: Arc<ControllerPool>,
    /// inspect node for peers to attach to
    inspect: inspect::Node,
}

const INITIATOR_DELAY: zx::Duration = zx::Duration::from_seconds(2);

impl Peers {
    pub fn new(
        streams: stream::Streams,
        negotiation: CodecNegotiation,
        profile: bredr::ProfileProxy,
        channel_mode: bredr::ChannelMode,
        controller: Arc<ControllerPool>,
    ) -> Self {
        Peers {
            peers: DetachableMap::new(),
            inspect: inspect::Node::default(),
            profile,
            streams,
            negotiation,
            channel_mode,
            controller,
        }
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
        negotiation: CodecNegotiation,
        profile: bredr::ProfileProxy,
        controller_pool: Arc<ControllerPool>,
        inspect: &inspect::Node,
    ) -> Result<(), Error> {
        let id = entry.key();
        let avdtp_peer = avdtp::Peer::new(channel);
        let mut peer = Peer::create(id.clone(), avdtp_peer, streams, profile, None);
        if let Err(e) = peer.iattach(inspect, inspect::unique_name("peer_")) {
            info!("Couldn't attach peer to inspect: {:?}", e);
        }
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
                info!("Peer connected to us, aborting.");
                return Err(format_err!("Couldn't start peer"));
            }
        };

        // Alert the ControllerPool of a new peer.
        controller_pool.peer_connected(id.clone(), detached_peer.clone());

        if start_streaming_flag {
            Peers::spawn_streaming(entry.clone(), negotiation);
        }

        let peer_id = id.clone();
        fasync::Task::local(async move {
            closed_fut.await;
            info!("Detaching closed peer {}", peer_id);
            detached_peer.detach();
        })
        .detach();
        Ok(())
    }

    /// Called when a peer is discovered via SDP.
    pub fn discovered(&mut self, id: PeerId, desc: bredr::ProfileDescriptor) {
        let entry = self.peers.lazy_entry(&id);
        let profile = self.profile.clone();
        let streams = self.streams.as_new();
        let channel_mode = self.channel_mode.clone();
        let controller_pool = self.controller.clone();
        let inspect = self.inspect.clone_weak();
        let negotiation = self.negotiation.clone();
        fasync::Task::local(async move {
            info!("Waiting {:?} to connect to discovered peer {}", INITIATOR_DELAY, id);
            fasync::Timer::new(INITIATOR_DELAY.after_now()).await;
            if let Some(peer) = entry.get() {
                info!("After initiator delay, {} was connected, not connecting..", id);
                if let Some(peer) = peer.upgrade() {
                    if peer.set_descriptor(desc.clone()).is_none() {
                        // TODO(fxbug.dev/50465): maybe check to see if we should start streaming.
                        Peers::spawn_streaming(entry.clone(), negotiation);
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
            if let Err(e) = Peers::add_control_connection(
                entry,
                channel,
                Some(desc),
                streams,
                negotiation,
                profile,
                controller_pool,
                &inspect,
            ) {
                warn!("Error adding control connection for {}: {:?}", id, e);
            }
        })
        .detach();
    }

    /// Called when a peer initiates a connection. If it is the first active connection, it creates
    /// a new Peer to handle communication.
    pub fn connected(&mut self, id: PeerId, channel: Channel) -> Result<(), Error> {
        let entry = self.peers.lazy_entry(&id);
        if let Some(peer) = entry.get() {
            if let Some(peer) = peer.upgrade() {
                if let Err(e) = peer.receive_channel(channel) {
                    warn!("{} connected an unexpected channel: {}", id, e);
                }
            }
            return Ok(());
        }
        Peers::add_control_connection(
            entry,
            channel,
            None,
            self.streams.as_new(),
            self.negotiation.clone(),
            self.profile.clone(),
            self.controller.clone(),
            &self.inspect,
        )
    }

    /// Attempt to start a media stream to `peer`
    fn spawn_streaming(entry: LazyEntry<PeerId, Peer>, negotiation: CodecNegotiation) {
        let weak_peer = match entry.get() {
            None => return,
            Some(peer) => peer,
        };
        fuchsia_async::Task::local(async move {
            if let Err(e) = start_streaming(&weak_peer, &negotiation).await {
                info!("Failed to stream: {:?}", e);
                weak_peer.detach();
            }
        })
        .detach();
    }
}

impl Inspect for &mut Peers {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name);
        Ok(())
    }
}

async fn start_streaming(
    peer: &DetachableWeak<PeerId, Peer>,
    negotiation: &CodecNegotiation,
) -> Result<(), anyhow::Error> {
    let streams_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.collect_capabilities()
    };
    let remote_streams = streams_fut.await?;

    let (codec_caps, remote_seid) =
        negotiation.select(&remote_streams).ok_or(format_err!("No compatible stream found"))?;

    let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
    strong.stream_start(remote_seid, codec_caps).await.map_err(Into::into)
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy_and_stream;
    use fuchsia_inspect::assert_inspect_tree;

    fn setup_peers_test() -> (fasync::Executor, PeerId, Peers, bredr::ProfileRequestStream) {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (proxy, stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");
        let controller = Arc::new(ControllerPool::new());
        let id = PeerId(1);

        let negotiation = CodecNegotiation::build(vec![]).expect("negotiation builds");
        let peers = Peers::new(
            stream::Streams::new(),
            negotiation,
            proxy,
            bredr::ChannelMode::Basic,
            controller,
        );

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
    fn peers_inspectable() {
        let (_exec, id, mut peers, _stream) = setup_peers_test();
        let inspect = inspect::Inspector::new();
        peers.iattach(inspect.root(), "peers").expect("should attach to inspect tree");

        assert_inspect_tree!(inspect, root: { peers: {} });

        // Connect a peer, it should show up in the tree.
        let (_remote, transport) = Channel::create();
        peers.connected(id, transport).expect("should be able to add peer");

        assert_inspect_tree!(inspect, root: { peers: { peer_0: {
            id: "0000000000000001", local_streams: contains {}
        }}});
    }
}
