// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_a2dp::{media_types::*, peer::ControllerPool, peer::Peer, stream},
    bt_avdtp::{self as avdtp, ServiceCapability, ServiceCategory},
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_bluetooth::{
        detachable_map::{DetachableMap, DetachableWeak, LazyEntry},
        types::{Channel, PeerId},
    },
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
    fuchsia_syslog::{self, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    std::{
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
};

use crate::{AAC_SEID, SBC_SEID};

pub struct Peers {
    peers: DetachableMap<PeerId, Peer>,
    streams: stream::Streams,
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
        profile: bredr::ProfileProxy,
        channel_mode: bredr::ChannelMode,
        controller: Arc<ControllerPool>,
    ) -> Self {
        Peers {
            peers: DetachableMap::new(),
            inspect: inspect::Node::default(),
            profile,
            streams,
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
        profile: bredr::ProfileProxy,
        controller_pool: Arc<ControllerPool>,
        inspect: &inspect::Node,
    ) -> Result<(), Error> {
        let id = entry.key();
        let avdtp_peer = avdtp::Peer::new(channel);
        let mut peer = Peer::create(id.clone(), avdtp_peer, streams, profile, None);
        if let Err(e) = peer.iattach(inspect, inspect::unique_name("peer_")) {
            fx_log_info!("Couldn't attach peer to inspect: {:?}", e);
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
                fx_log_info!("Peer connected to us, aborting.");
                return Err(format_err!("Couldn't start peer"));
            }
        };

        // Alert the ControllerPool of a new peer.
        controller_pool.peer_connected(id.clone(), detached_peer.clone());

        if start_streaming_flag {
            Peers::spawn_streaming(entry.clone());
        }

        let peer_id = id.clone();
        fasync::Task::local(async move {
            closed_fut.await;
            fx_log_info!("Detaching closed peer {}", peer_id);
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
        fasync::Task::local(async move {
            fx_log_info!("Waiting {:?} to connect to discovered peer {}", INITIATOR_DELAY, id);
            fasync::Timer::new(INITIATOR_DELAY.after_now()).await;
            if let Some(peer) = entry.get() {
                fx_log_info!("After initiator delay, {} was connected, not connecting..", id);
                if let Some(peer) = peer.upgrade() {
                    if peer.set_descriptor(desc.clone()).is_none() {
                        // TODO(50465): maybe check to see if we should start streaming.
                        Peers::spawn_streaming(entry.clone());
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
                    fx_log_warn!("FIDL error connecting to peer {}: {:?}", id, e);
                    return;
                }
                Ok(Err(code)) => {
                    fx_log_info!("Couldn't connect to peer {}: {:?}", id, code);
                    return;
                }
                Ok(Ok(channel)) => channel,
            };
            let channel = match channel.try_into() {
                Ok(chan) => chan,
                Err(e) => {
                    fx_log_warn!("No channel from peer {}: {:?}", id, e);
                    return;
                }
            };
            if let Err(e) = Peers::add_control_connection(
                entry,
                channel,
                Some(desc),
                streams,
                profile,
                controller_pool,
                &inspect,
            ) {
                fx_log_warn!("Error adding control connection for {}: {:?}", id, e);
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
                    fx_log_warn!("{} connected an unexpected channel: {}", id, e);
                }
            }
            return Ok(());
        }
        Peers::add_control_connection(
            entry,
            channel,
            None,
            self.streams.as_new(),
            self.profile.clone(),
            self.controller.clone(),
            &self.inspect,
        )
    }

    /// Attempt to start a media stream to `peer`
    fn spawn_streaming(entry: LazyEntry<PeerId, Peer>) {
        let weak_peer = match entry.get() {
            None => return,
            Some(peer) => peer,
        };
        fuchsia_async::Task::local(async move {
            if let Err(e) = start_streaming(&weak_peer).await {
                fx_log_info!("Failed to stream: {:?}", e);
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

async fn start_streaming(peer: &DetachableWeak<PeerId, Peer>) -> Result<(), anyhow::Error> {
    let streams_fut = {
        let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
        strong.collect_capabilities()
    };
    let remote_streams = streams_fut.await?;
    let selected_stream = SelectedStream::pick(&remote_streams)?;

    let strong = peer.upgrade().ok_or(format_err!("Disconnected"))?;
    strong
        .stream_start(
            selected_stream.seid.try_into()?,
            selected_stream.remote_stream.local_id().clone(),
            selected_stream.codec_settings.clone(),
        )
        .await
        .map_err(Into::into)
}

/// Pick a reasonable quality bitrate to use by default. 64k average per channel.
const PREFERRED_BITRATE_AAC: u32 = 128000;

/// Represents a chosen remote stream endpoint and negotiated codec and encoder settings
#[derive(Debug)]
struct SelectedStream<'a> {
    remote_stream: &'a avdtp::StreamEndpoint,
    codec_settings: ServiceCapability,
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
    ) -> Result<ServiceCapability, Error> {
        let codec_cap = Self::get_codec_cap(stream).ok_or(format_err!("no codec extra found"))?;
        match codec_cap {
            ServiceCapability::MediaCodec {
                codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                codec_extra,
                ..
            } => {
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
                Ok(ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                    codec_extra: aac_codec_info.to_bytes().to_vec(),
                })
            }
            ServiceCapability::MediaCodec {
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra,
                ..
            } => {
                // TODO(39321): Choose codec options based on availability and quality.
                let remote_codec_info = SbcCodecInfo::try_from(&codec_extra[..])?;
                // Attempt to use the recommended "High Quality" bitpool value.
                let mut bitpool_value = 51;
                bitpool_value = std::cmp::min(bitpool_value, remote_codec_info.max_bitpool());
                let sbc_codec_info = SbcCodecInfo::new(
                    SbcSamplingFrequency::FREQ48000HZ,
                    SbcChannelMode::JOINT_STEREO,
                    SbcBlockCount::SIXTEEN,
                    SbcSubBands::EIGHT,
                    SbcAllocation::LOUDNESS,
                    /* min_bpv= */ bitpool_value,
                    /* max_bpv= */ bitpool_value,
                )?;

                Ok(ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                })
            }
            ServiceCapability::MediaCodec { codec_type, .. } => {
                Err(format_err!("Unsupported codec {:?}", codec_type))
            }
            x => Err(format_err!("Expected MediaCodec and got {:?}", x)),
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

    fn get_codec_cap(stream: &'a avdtp::StreamEndpoint) -> Option<&'a ServiceCapability> {
        stream.capabilities().iter().find(|cap| cap.category() == ServiceCategory::MediaCodec)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{build_aac_endpoint, build_sbc_endpoint};

    use fidl::endpoints::create_proxy_and_stream;
    use fuchsia_inspect::assert_inspect_tree;
    use matches::assert_matches;

    fn setup_peers_test() -> (fasync::Executor, PeerId, Peers, bredr::ProfileRequestStream) {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        let (proxy, stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");
        let controller = Arc::new(ControllerPool::new());
        let id = PeerId(1);

        let peers =
            Peers::new(stream::Streams::new(), proxy, bredr::ChannelMode::Basic, controller);

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

    #[test]
    fn test_stream_selection() {
        let sbc_endpoint = build_sbc_endpoint(avdtp::EndpointType::Sink).expect("sbc endpoint");
        let aac_endpoint = build_aac_endpoint(avdtp::EndpointType::Sink).expect("aac endpoint");

        // test that aac is preferred
        let remote_streams = vec![aac_endpoint.as_new(), sbc_endpoint.as_new()];
        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: AAC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
                        ..
                    },
                ..
            })
        );

        // only sbc available, should return sbc
        let remote_streams = vec![sbc_endpoint.as_new()];
        assert_matches!(
            SelectedStream::pick(&remote_streams),
            Ok(SelectedStream {
                seid: SBC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        ..
                    },
                ..
            })
        );

        // none available, should error
        let remote_streams = vec![];
        assert!(SelectedStream::pick(&remote_streams).is_err());

        // When remote end has a bitpool max that is lower than our preferred, the selected bitpool
        // value doesn't go out of range.
        let lower_max_bpv = 45;
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            lower_max_bpv,
        )
        .expect("sbc_codec_info");
        let remote_streams = vec![avdtp::StreamEndpoint::new(
            SBC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                ServiceCapability::MediaTransport,
                ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_codec_info.to_bytes().to_vec(),
                },
            ],
        )
        .expect("stream endpoint")];

        match SelectedStream::pick(&remote_streams) {
            Ok(SelectedStream {
                seid: SBC_SEID,
                codec_settings:
                    ServiceCapability::MediaCodec {
                        media_type: avdtp::MediaType::Audio,
                        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                        codec_extra,
                    },
                ..
            }) => {
                let codec_info =
                    SbcCodecInfo::try_from(&codec_extra[..]).expect("codec extra to parse");
                assert!(codec_info.max_bitpool() <= lower_max_bpv);
            }
            x => panic!("Expected Ok Selected SBC stream, got {:?}", x),
        };
    }
}
