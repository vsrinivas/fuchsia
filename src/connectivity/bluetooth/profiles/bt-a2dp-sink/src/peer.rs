// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp::{self as avdtp, ServiceCapability, StreamEndpoint, StreamEndpointId},
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_bredr::{ChannelParameters, ProfileDescriptor, ProfileProxy, PSM_AVDTP},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        task::{Context, Poll, Waker},
        Future, StreamExt,
    },
    parking_lot::Mutex,
    std::pin::Pin,
    std::sync::{Arc, Weak},
};

use crate::inspect_types::{RemoteCapabilitiesInspect, RemotePeerInspect};
use crate::Streams;

pub(crate) struct Peer {
    /// The id of the peer we are connected to.
    id: PeerId,
    /// The inner peer
    inner: Arc<Mutex<PeerInner>>,
    /// Profile Proxy, used to connect new transport sockets.
    profile: ProfileProxy,
    /// The profile descriptor for this peer, if it has been discovered.
    descriptor: Option<ProfileDescriptor>,
}

impl Peer {
    /// Make a new Peer which is connected to the peer `id` using the AVDTP `peer`.
    /// The `streams` are the local endpoints available to the peer.
    /// The `inspect` is the inspect node associated with the peer.
    pub fn create(
        id: PeerId,
        peer: avdtp::Peer,
        streams: Streams,
        profile: ProfileProxy,
        inspect: inspect::Node,
        cobalt_sender: CobaltSender,
    ) -> Self {
        let res = Self {
            id,
            inner: Arc::new(Mutex::new(PeerInner::new(peer, streams, inspect, cobalt_sender))),
            profile,
            descriptor: None,
        };

        res.start_requests_task();
        res
    }

    pub fn set_descriptor(&mut self, descriptor: ProfileDescriptor) {
        self.descriptor = Some(descriptor);
    }

    /// Receive a channel from the peer that was initiated remotely.
    /// This function should be called whenever the peer associated with this opens an L2CAP channel.
    pub fn receive_channel(&self, channel: zx::Socket) -> avdtp::Result<()> {
        let mut lock = self.inner.lock();
        lock.receive_channel(channel)
    }

    /// Return a handle to the AVDTP peer, to use as initiator of commands.
    pub fn avdtp_peer(&self) -> Arc<avdtp::Peer> {
        let lock = self.inner.lock();
        lock.peer.clone()
    }

    pub fn remote_capabilities_inspect(&self) -> RemoteCapabilitiesInspect {
        let lock = self.inner.lock();
        lock.inspect.remote_capabilities_inspect()
    }

    pub fn cobalt_logger(&self) -> CobaltSender {
        let lock = self.inner.lock();
        lock.cobalt_sender.clone()
    }

    /// Perform Discovery and then Capability detection to discover the capabilities of the
    /// connected peer's stream endpoints
    /// Returns a map of remote stream endpoint ids associated with the MediaCodec service capability of
    /// the endpoint.
    pub fn collect_capabilities(&self) -> impl Future<Output = avdtp::Result<Vec<StreamEndpoint>>> {
        let avdtp = self.inner.lock().peer.clone();
        let get_all = self.descriptor.map_or(false, a2dp_version_check);
        async move {
            fx_vlog!(1, "Discovering peer streams..");
            let infos = avdtp.discover().await?;
            fx_vlog!(1, "Discovered {} streams", infos.len());
            let mut remote_streams = Vec::new();
            for info in infos {
                let capabilities = if get_all {
                    avdtp.get_all_capabilities(info.id()).await
                } else {
                    avdtp.get_capabilities(info.id()).await
                };
                match capabilities {
                    Ok(capabilities) => {
                        fx_vlog!(1, "Stream {:?}", info);
                        for cap in &capabilities {
                            fx_vlog!(1, "  - {:?}", cap);
                        }
                        remote_streams.push(avdtp::StreamEndpoint::from_info(&info, capabilities));
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} capabilities failed: {:?}, skipping", info.id(), e);
                    }
                };
            }

            Ok(remote_streams)
        }
    }

    /// Open and start a media transport stream, connecting the local stream `local_id` to the
    /// remote stream `remote_id`, configuring it with the MediaCodec capability.
    /// Returns the MediaStream which can either be streamed from, or an error.
    pub fn start_stream(
        &self,
        local_id: StreamEndpointId,
        remote_id: StreamEndpointId,
        codec_params: ServiceCapability,
    ) -> impl Future<Output = avdtp::Result<()>> {
        let peer = Arc::downgrade(&self.inner);
        let lock = self.inner.lock();
        let peer_id = self.id.clone();
        let avdtp = lock.peer.clone();
        let profile = self.profile.clone();
        async move {
            fx_vlog!(
                1,
                "Connecting stream {} to remote stream {} with {:?}",
                local_id,
                remote_id,
                codec_params
            );
            let capabilities = vec![ServiceCapability::MediaTransport, codec_params];
            avdtp.set_configuration(&remote_id, &local_id, &capabilities).await?;
            avdtp.open(&remote_id).await?;
            {
                let strong = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                strong.lock().set_opening(&local_id, &remote_id, capabilities)?;
            }
            let (status, channel) = profile
                .connect_l2cap(
                    &peer_id.to_string(),
                    PSM_AVDTP as u16,
                    ChannelParameters::new_empty(),
                )
                .await
                .expect("FIDL error: {}");
            if let Some(e) = status.error {
                fx_log_warn!("Couldn't connect media transport {}: {:?}", peer_id, e);
                return Err(avdtp::Error::PeerDisconnected);
            }
            if channel.socket.is_none() {
                fx_log_warn!("Couldn't connect media transport {}: no channel", peer_id);
                return Err(avdtp::Error::PeerDisconnected);
            }
            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.receive_channel(channel.socket.unwrap())?;
            }
            let to_start = &[remote_id];
            avdtp.start(to_start).await?;
            {
                // Start the media decoding task with this call to start_endpoint()).
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.start_endpoint(&local_id).map_err(|e| avdtp::Error::RequestInvalid(e))
            }
        }
    }

    /// Start an asynchronous task to handle any requests from the AVDTP peer.
    /// This task completes when the remote end closes the signaling connection.
    fn start_requests_task(&self) {
        let lock = self.inner.lock();
        let mut request_stream = lock.peer.take_request_stream();
        let id = self.id.clone();
        let peer = Arc::downgrade(&self.inner);
        fuchsia_async::spawn_local(async move {
            while let Some(r) = request_stream.next().await {
                match r {
                    Err(e) => fx_log_info!("Request Error on {}: {:?}", id, e),
                    Ok(request) => match peer.upgrade() {
                        None => {
                            fx_log_info!("Peer disappeared processing requests, ending");
                            return;
                        }
                        Some(p) => {
                            let mut lock = p.lock();
                            if let Err(e) = lock.handle_request(request).await {
                                fx_log_warn!("{} Error handling request: {:?}", id, e);
                            }
                        }
                    },
                }
            }
            fx_log_info!("Peer {} disconnected", id);
            peer.upgrade().map(|p| p.lock().disconnected());
        });
    }

    /// Returns a future that will complete when the peer disconnects.
    pub fn closed(&self) -> ClosedPeer {
        ClosedPeer { inner: Arc::downgrade(&self.inner) }
    }
}

/// Future for the closed() future.
pub struct ClosedPeer {
    inner: Weak<Mutex<PeerInner>>,
}

#[must_use = "futures do nothing unless you `.await` or poll them"]
impl Future for ClosedPeer {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.inner.upgrade() {
            None => Poll::Ready(()),
            Some(inner) => match inner.lock().closed_wakers.as_mut() {
                None => Poll::Ready(()),
                Some(wakers) => {
                    wakers.push(cx.waker().clone());
                    Poll::Pending
                }
            },
        }
    }
}

/// Determines if Peer profile version is newer (>= 1.3) or older (< 1.3)
fn a2dp_version_check(profile: ProfileDescriptor) -> bool {
    (profile.major_version == 1 && profile.minor_version >= 3) || profile.major_version > 1
}

/// Peer handles the communication with the AVDTP layer, and provides responses as appropriate
/// based on the current state of local streams available.
/// Each peer has its own set of local stream endpoints, and tracks a set of remote peer endpoints.
struct PeerInner {
    /// AVDTP peer communicating to this.
    peer: Arc<avdtp::Peer>,
    /// Some(id) if we are opening a StreamEndpoint but haven't finished yet.
    /// This is the local ID.
    /// AVDTP Sec 6.11 - only up to one stream can be in this state.
    opening: Option<StreamEndpointId>,
    /// The local stream endpoint collection
    local: Streams,
    /// The inspect data for this peer.
    inspect: RemotePeerInspect,
    /// Wakers that are to be woken when the peer disconnects.  If None, the peers have been woken
    /// and this peer is disconnected.
    closed_wakers: Option<Vec<Waker>>,
    /// The cobalt logger for this peer
    cobalt_sender: CobaltSender,
}

impl PeerInner {
    fn new(
        peer: avdtp::Peer,
        mut streams: Streams,
        inspect: inspect::Node,
        cobalt_sender: CobaltSender,
    ) -> Self {
        // Setup inspect nodes for the remote peer and for each of the streams that it holds
        let mut inspect = RemotePeerInspect::new(inspect);
        for (id, stream) in streams.iter_mut() {
            let stream_state_property = inspect.create_stream_state_inspect(id);
            let callback = move |stream: &avdtp::StreamEndpoint| {
                stream_state_property.set(&format!("{:?}", stream))
            };
            stream.set_endpoint_update_callback(Some(Box::new(callback)));
        }
        Self {
            peer: Arc::new(peer),
            opening: None,
            local: streams,
            inspect,
            closed_wakers: Some(Vec::new()),
            cobalt_sender,
        }
    }

    /// Returns an endpoint from the local set or a BadAcpSeid error if it doesn't exist.
    fn get_mut(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<&mut StreamEndpoint> {
        self.local
            .get_endpoint(&local_id)
            .ok_or(avdtp::Error::RequestInvalid(avdtp::ErrorCode::BadAcpSeid))
    }

    /// Configures the remote stream endpoint with the provided capabilities and
    /// sets the opening of the stream.
    fn set_opening(
        &mut self,
        local_id: &StreamEndpointId,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        let stream = self.get_mut(&local_id)?;
        stream.configure(&remote_id, capabilities)?;
        stream.establish()?;
        self.opening = Some(local_id.clone());
        Ok(())
    }

    // Starts the media decoding task for a stream |seid|.
    fn start_endpoint(&mut self, seid: &avdtp::StreamEndpointId) -> Result<(), avdtp::ErrorCode> {
        let inspect = &mut self.inspect;
        let cobalt_sender = self.cobalt_sender.clone();
        self.local.get_mut(&seid).and_then(|stream| {
            let inspect = inspect.create_streaming_inspect_data(&seid);
            stream.start(inspect, cobalt_sender).or(Err(avdtp::ErrorCode::BadState))
        })
    }

    /// To be called when the peer disconnects. Wakes waiters on the closed peer.
    fn disconnected(&mut self) {
        for waker in self.closed_wakers.take().unwrap_or_else(Vec::new) {
            waker.wake();
        }
    }

    /// Provide a new established L2CAP channel to this remote peer.
    /// This function should be called whenever the remote associated with this peer opens an
    /// L2CAP channel after the first.
    fn receive_channel(&mut self, channel: zx::Socket) -> avdtp::Result<()> {
        let stream_id = self.opening.as_ref().cloned().ok_or(avdtp::Error::InvalidState)?;
        let stream = self.get_mut(&stream_id)?;
        let channel =
            fasync::Socket::from_socket(channel).or_else(|e| Err(avdtp::Error::ChannelSetup(e)))?;
        if !stream.receive_channel(channel)? {
            self.opening = None;
        }
        fx_log_info!("Transport channel connected to seid {}", stream_id);
        Ok(())
    }

    /// Handle a single request event from the avdtp peer.
    async fn handle_request(&mut self, r: avdtp::Request) -> avdtp::Result<()> {
        fx_vlog!(1, "Handling {:?} from peer..", r);
        match r {
            avdtp::Request::Discover { responder } => responder.send(&self.local.information()),
            avdtp::Request::GetCapabilities { responder, stream_id }
            | avdtp::Request::GetAllCapabilities { responder, stream_id } => {
                match self.local.get_endpoint(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => responder.send(stream.capabilities()),
                }
            }
            avdtp::Request::Open { responder, stream_id } => {
                match self.local.get_endpoint(&stream_id) {
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
                match self.local.get_endpoint(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream.release(responder, &self.peer).await,
                }
            }
            avdtp::Request::SetConfiguration {
                responder,
                local_stream_id,
                remote_stream_id,
                capabilities,
            } => {
                let stream = match self.local.get_endpoint(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(BT-695): Confirm the MediaCodec parameters are OK
                match stream.configure(&remote_stream_id, capabilities.clone()) {
                    Ok(_) => responder.send(),
                    Err(e) => {
                        // Only happens when this is already configured.
                        responder.reject(None, avdtp::ErrorCode::SepInUse)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::GetConfiguration { stream_id, responder } => {
                let stream = match self.local.get_endpoint(&stream_id) {
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
                let stream = match self.local.get_endpoint(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(40768): Actually tweak the codec parameters.
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
                    let res = self.start_endpoint(&seid);
                    if let Err(code) = res {
                        return responder.reject(&seid, code);
                    }
                }
                responder.send()
            }
            avdtp::Request::Suspend { responder, stream_ids } => {
                for seid in stream_ids {
                    let res = self
                        .local
                        .get_mut(&seid)
                        .and_then(|x| x.stop().or(Err(avdtp::ErrorCode::BadState)));
                    if let Err(code) = res {
                        return responder.reject(&seid, code);
                    }
                }
                responder.send()
            }
            avdtp::Request::Abort { responder, stream_id } => {
                let stream = match self.local.get_endpoint(&stream_id) {
                    // No response shall be sent if the SEID is not valid. AVDTP 8.16.2
                    None => return Ok(()),
                    Some(stream) => stream,
                };
                stream.abort(None).await?;
                self.opening = self.opening.take().filter(|id| id != &stream_id);
                let _ = self
                    .local
                    .get_mut(&stream_id)
                    .and_then(|x| x.stop().or(Err(avdtp::ErrorCode::BadState)));
                responder.send()
            }
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use bt_a2dp::media_types::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth::Status;
    use fidl_fuchsia_bluetooth_bredr::{
        Channel, ProfileMarker, ProfileRequest, ServiceClassProfileIdentifier,
    };
    use fidl_fuchsia_cobalt::CobaltEvent;
    use futures::channel::mpsc;
    use futures::pin_mut;
    use std::convert::TryInto;

    use crate::SBC_SEID;

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn setup_avdtp_peer() -> (avdtp::Peer, zx::Socket) {
        let (remote, signaling) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let peer = avdtp::Peer::new(signaling).expect("create peer failure");
        (peer, remote)
    }

    fn create_streams() -> Streams {
        let mut s = Streams::new();
        let sbc_media_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::MANDATORY_SNK,
            SbcChannelMode::MANDATORY_SNK,
            SbcBlockCount::MANDATORY_SNK,
            SbcSubBands::MANDATORY_SNK,
            SbcAllocation::MANDATORY_SNK,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .expect("Building codec info should work");
        let sbc_stream = avdtp::StreamEndpoint::new(
            SBC_SEID,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Sink,
            vec![
                avdtp::ServiceCapability::MediaTransport,
                avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: sbc_media_codec_info.to_bytes().to_vec(),
                },
            ],
        )
        .expect("Building endpoint shoul work");
        s.insert(sbc_stream);
        s
    }

    fn receive_simple_accept(remote: &zx::Socket, signal_id: u8) {
        let received = recv_remote(&remote).expect("expected a packet");
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(signal_id, received[1]);
        let txlabel_raw = received[0] & 0xF0;
        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            signal_id,
        ];
        assert!(remote.write(response).is_ok());
    }

    pub(crate) fn recv_remote(remote: &zx::Socket) -> Result<Vec<u8>, zx::Status> {
        let waiting = remote.outstanding_read_bytes();
        assert!(waiting.is_ok());
        let mut response: Vec<u8> = vec![0; waiting.unwrap()];
        let response_read = remote.read(response.as_mut_slice())?;
        assert_eq!(response.len(), response_read);
        Ok(response)
    }

    #[test]
    fn test_disconnected() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, _stream) =
            create_proxy::<ProfileMarker>().expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();

        let (remote, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        let id = PeerId(1);

        let avdtp = avdtp::Peer::new(signaling).expect("peer should be creatable");
        let inspect = inspect::Inspector::new();
        let inspect = inspect.root().create_child(format!("peer {}", id));

        let peer = Peer::create(id, avdtp, Streams::new(), proxy, inspect, cobalt_sender);

        let closed_fut = peer.closed();

        pin_mut!(closed_fut);

        assert!(exec.run_until_stalled(&mut closed_fut).is_pending());

        // Close the remote socket.
        drop(remote);

        assert!(exec.run_until_stalled(&mut closed_fut).is_ready());
    }

    #[test]
    // TODO(44645): Refactor this into common location with A2DP-Source.
    fn test_peer_start_stream_success() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, mut prof_stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();

        let id = PeerId(1);
        let (avdtp, remote) = setup_avdtp_peer();
        let inspect = inspect::Inspector::new();
        let inspect = inspect.root().create_child(format!("peer {}", id));
        let streams = create_streams();
        let peer = Peer::create(id, avdtp, streams, proxy, inspect, cobalt_sender);

        // This needs to match the local SBC_SEID
        let local_seid = SBC_SEID.try_into().unwrap();
        let remote_seid = 2_u8.try_into().unwrap();
        let codec_params = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x29, 0xF5, 2, 250],
        };
        let start_future = peer.start_stream(local_seid, remote_seid, codec_params);
        pin_mut!(start_future);
        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x03); // Set Configuration
        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x06); // Open
        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => {}
            Poll::Ready(Err(e)) => panic!("Expected to be pending but error: {:?}", e),
            Poll::Ready(Ok(_)) => panic!("Expected to be pending but finished!"),
        };

        // Should connect the media socket after open.
        let (_, transport) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let request = exec.run_until_stalled(&mut prof_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::ConnectL2cap { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.parse().expect("peer_id parses"));
                responder
                    .send(
                        &mut Status { error: None },
                        Channel { socket: Some(transport), ..Channel::new_empty() },
                    )
                    .expect("responder sends");
            }
            x => panic!("Should have sent a open l2cap request, but got {:?}", x),
        };

        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => {}
            Poll::Ready(Err(e)) => panic!("Expected to be pending but error: {:?}", e),
            Poll::Ready(Ok(_)) => panic!("Expected to be pending but finished!"),
        };
        receive_simple_accept(&remote, 0x07); // Start
                                              // Should return the media stream (which should be connected)
                                              // Should be done without an error, but with no streams.

        let res = exec.run_until_stalled(&mut start_future);
        match res {
            Poll::Pending => panic!("Should be ready after start succeeds"),
            Poll::Ready(Err(e)) => panic!("Shouldn't be an error but returned {:?}", e),
            // TODO: confirm the stream is usable
            Poll::Ready(Ok(_stream)) => {}
        }
    }

    #[test]
    /// Test if the version check correctly returns the flag
    fn test_a2dp_version_check() {
        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        let res = a2dp_version_check(p1);
        assert_eq!(true, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 10,
        };
        let res = a2dp_version_check(p1);
        assert_eq!(true, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 0,
        };
        let res = a2dp_version_check(p1);
        assert_eq!(false, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 0,
            minor_version: 9,
        };
        let res = a2dp_version_check(p1);
        assert_eq!(false, res);

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 2,
        };
        let res = a2dp_version_check(p1);
        assert_eq!(true, res);
    }
}
