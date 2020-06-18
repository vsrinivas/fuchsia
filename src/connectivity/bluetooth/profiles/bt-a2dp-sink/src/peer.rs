// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_a2dp::stream::Streams,
    bt_a2dp_sink_metrics as metrics,
    bt_avdtp::{
        self as avdtp, MediaCodecType, ServiceCapability, ServiceCategory, StreamEndpoint,
        StreamEndpointId,
    },
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_bredr::{ChannelParameters, ProfileDescriptor, ProfileProxy, PSM_AVDTP},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect as inspect,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        task::{Context, Poll, Waker},
        Future, StreamExt,
    },
    parking_lot::Mutex,
    std::{
        collections::HashSet,
        pin::Pin,
        sync::{Arc, Weak},
    },
};

use crate::inspect_types::RemotePeerInspect;

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
        // populate the higher level "id" inspect field before passing the inspect node to
        // `PeerInner` because the concept of a `PeerId` does not exist at that level.
        inspect.record_string("id", id.to_string());

        let res = Self {
            id,
            inner: Arc::new(Mutex::new(PeerInner::new(id, peer, streams, inspect, cobalt_sender))),
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

    /// Perform Discovery and then Capability detection to discover the capabilities of the
    /// connected peer's stream endpoints
    /// Returns a vector of stream endpoints whose local_ids are the peer's stream ids.
    /// the endpoint.
    pub fn get_remote_capabilities(
        &self,
    ) -> impl Future<Output = avdtp::Result<Vec<StreamEndpoint>>> {
        let avdtp = self.avdtp_peer();
        let inner = self.inner.clone();
        let get_all = self.descriptor.map_or(false, a2dp_version_check);
        async move {
            {
                let lock = inner.lock();
                if let Some(caps) = lock.cached_remote_endpoints.as_ref() {
                    return Ok(caps.iter().map(StreamEndpoint::as_new).collect());
                }
            }
            fx_vlog!(1, "Discovering peer streams..");
            let infos = avdtp.discover().await?;
            fx_vlog!(1, "Discovered {} streams", infos.len());
            let mut streams = Vec::new();
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
                        streams.push(StreamEndpoint::from_info(&info, capabilities));
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} capabilities failed: {:?}, skipping", info.id(), e);
                    }
                };
            }
            inner.lock().set_remote_endpoints(&streams).await;

            Ok(streams)
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
            let channel = profile
                .connect(&mut peer_id.into(), PSM_AVDTP, ChannelParameters::new_empty())
                .await
                .expect("FIDL client error: {}");

            let channel = match channel {
                Err(e) => {
                    fx_log_warn!("Couldn't connect media transport {}: {:?}", peer_id, e);
                    return Err(avdtp::Error::PeerDisconnected);
                }
                Ok(channel) => channel,
            };

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
    /// The peer id that `peer` is connected to.
    peer_id: PeerId,
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
    /// Cached vector of remote StreamEndpoints, if they have been set.
    /// These Endpoints are all in the new state.
    cached_remote_endpoints: Option<Vec<StreamEndpoint>>,
}

impl PeerInner {
    fn new(
        peer_id: PeerId,
        peer: avdtp::Peer,
        mut streams: Streams,
        inspect: inspect::Node,
        cobalt_sender: CobaltSender,
    ) -> Self {
        // Setup inspect nodes for the remote peer and for each of the streams that it holds
        let inspect = RemotePeerInspect::new(inspect, &mut streams);
        Self {
            peer: Arc::new(peer),
            peer_id,
            opening: None,
            local: streams,
            inspect,
            closed_wakers: Some(Vec::new()),
            cobalt_sender,
            cached_remote_endpoints: None,
        }
    }

    /// Configures the remote stream endpoint with the provided capabilities and
    /// sets the opening of the stream.
    fn set_opening(
        &mut self,
        local_id: &StreamEndpointId,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        let stream = self
            .local
            .get_mut(&local_id)
            .ok_or(avdtp::Error::RequestInvalid(avdtp::ErrorCode::BadAcpSeid))?;
        stream
            .configure(&self.peer_id, &remote_id, capabilities)
            .map_err(|(_, c)| avdtp::Error::RequestInvalid(c))?;
        stream.endpoint_mut().establish().or(Err(avdtp::Error::InvalidState))?;
        self.opening = Some(local_id.clone());
        Ok(())
    }

    /// Sets the cached remote endpoint set to `endpoints`.
    /// Updates the reported Inspect data to match.
    fn set_remote_endpoints(&mut self, endpoints: &[StreamEndpoint]) -> impl Future<Output = ()> {
        self.cached_remote_endpoints = Some(endpoints.iter().map(StreamEndpoint::as_new).collect());
        let codec_metrics: HashSet<_> = endpoints
            .iter()
            .filter_map(|endpoint| {
                endpoint.codec_type().map(|t| codectype_to_availability_metric(t) as u32)
            })
            .collect();
        for metric in codec_metrics {
            self.cobalt_sender.log_event(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, metric);
        }
        let inspect = self.inspect.remote_capabilities_inspect();
        let endpoints_copy: Vec<_> = endpoints.iter().map(StreamEndpoint::as_new).collect();
        async move { inspect.append(&endpoints_copy).await }
    }

    // Starts the media decoding task for a stream |seid|.
    fn start_endpoint(&mut self, seid: &StreamEndpointId) -> Result<(), avdtp::ErrorCode> {
        self.local.get_mut(&seid).ok_or(avdtp::ErrorCode::BadAcpSeid)?.start()
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
        let stream = self
            .local
            .get_mut(&stream_id)
            .ok_or(avdtp::Error::RequestInvalid(avdtp::ErrorCode::BadAcpSeid))?;
        let channel =
            fasync::Socket::from_socket(channel).or_else(|e| Err(avdtp::Error::ChannelSetup(e)))?;
        if !stream.endpoint_mut().receive_channel(channel)? {
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
                match self.local.get(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => responder.send(stream.endpoint().capabilities()),
                }
            }
            avdtp::Request::Open { responder, stream_id } => match self.local.get_mut(&stream_id) {
                None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                Some(stream) => match stream.endpoint_mut().establish() {
                    Ok(()) => {
                        self.opening = Some(stream_id);
                        responder.send()
                    }
                    Err(_) => responder.reject(avdtp::ErrorCode::BadState),
                },
            },
            avdtp::Request::Close { responder, stream_id } => {
                match self.local.get_mut(&stream_id) {
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
                let stream = match self.local.get_mut(&local_stream_id) {
                    None => {
                        return responder
                            .reject(ServiceCategory::None, avdtp::ErrorCode::BadAcpSeid)
                    }
                    Some(stream) => stream,
                };
                match stream.configure(&self.peer_id, &remote_stream_id, capabilities.clone()) {
                    Ok(_) => responder.send(),
                    Err((cat, code)) => responder.reject(cat, code),
                }
            }
            avdtp::Request::GetConfiguration { stream_id, responder } => {
                let stream = match self.local.get(&stream_id) {
                    None => return responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                match stream.endpoint().get_configuration() {
                    Some(c) => responder.send(&c),
                    None => responder.reject(avdtp::ErrorCode::BadState),
                }
            }
            avdtp::Request::Reconfigure { responder, local_stream_id, capabilities } => {
                let stream = match self.local.get_mut(&local_stream_id) {
                    None => {
                        return responder
                            .reject(ServiceCategory::None, avdtp::ErrorCode::BadAcpSeid)
                    }
                    Some(stream) => stream,
                };
                match stream.reconfigure(capabilities) {
                    Ok(_) => responder.send(),
                    Err((cat, code)) => responder.reject(cat, code),
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
                        .map(|x| x.suspend())
                        .unwrap_or(Err(avdtp::ErrorCode::BadAcpSeid));
                    if let Err(code) = res {
                        return responder.reject(&seid, code);
                    }
                }
                responder.send()
            }
            avdtp::Request::Abort { responder, stream_id } => {
                let stream = match self.local.get_mut(&stream_id) {
                    // No response shall be sent if the SEID is not valid. AVDTP 8.16.2
                    None => return Ok(()),
                    Some(stream) => stream,
                };
                stream.abort(None).await;
                self.opening = self.opening.take().filter(|id| id != &stream_id);
                responder.send()
            }
        }
    }
}

fn codectype_to_availability_metric(
    codec_type: &MediaCodecType,
) -> metrics::A2dpCodecAvailabilityMetricDimensionCodec {
    match codec_type {
        &MediaCodecType::AUDIO_SBC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Sbc,
        &MediaCodecType::AUDIO_MPEG12 => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Mpeg12,
        &MediaCodecType::AUDIO_AAC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Aac,
        &MediaCodecType::AUDIO_ATRAC => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Atrac,
        &MediaCodecType::AUDIO_NON_A2DP => {
            metrics::A2dpCodecAvailabilityMetricDimensionCodec::VendorSpecific
        }
        _ => metrics::A2dpCodecAvailabilityMetricDimensionCodec::Unknown,
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use bt_a2dp::media_task::tests::TestMediaTaskBuilder;
    use bt_a2dp::media_types::*;
    use bt_a2dp::stream;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
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
        let mut streams = Streams::new();
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

        let stream = stream::Stream::build(sbc_stream, TestMediaTaskBuilder::new().builder());
        streams.insert(stream);
        streams
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

        let inspect = inspect.root().create_child(inspect::unique_name("peer_"));

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
        let inspect = inspect.root().create_child(inspect::unique_name("peer_"));
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
            Poll::Ready(Some(Ok(ProfileRequest::Connect { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.into());
                responder
                    .send(&mut Ok(Channel { socket: Some(transport), ..Channel::new_empty() }))
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
    /// Test that the get_remote_capabilities works correctly
    fn test_peer_get_remote_capabilities() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, _prof_stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let (cobalt_sender, _) = fake_cobalt_sender();

        let id = PeerId(1);
        let (avdtp, remote) = setup_avdtp_peer();
        let inspect = inspect::Inspector::new();
        let inspect = inspect.root().create_child(inspect::unique_name("peer_"));
        let streams = create_streams();
        let peer = Peer::create(id, avdtp, streams, proxy, inspect, cobalt_sender);

        let get_remote_fut = peer.get_remote_capabilities();
        pin_mut!(get_remote_fut);
        assert!(exec.run_until_stalled(&mut get_remote_fut).is_pending());

        let remote = avdtp::Peer::new(remote).expect("create remote peer");

        let mut request_stream = remote.take_request_stream();

        let mut streams = create_streams();

        // Should have received a discover request, then a request to get the capabilities of all
        // the things.
        match exec.run_until_stalled(&mut request_stream.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::Discover { responder }))) => {
                responder.send(&streams.information()).expect("discover response success");
            }
            x => panic!("Expected an avdtp discovery request, got {:?}", x),
        };

        assert!(exec.run_until_stalled(&mut get_remote_fut).is_pending());

        match exec.run_until_stalled(&mut request_stream.next()) {
            Poll::Ready(Some(Ok(avdtp::Request::GetCapabilities { stream_id, responder }))) => {
                let sbc_seid: StreamEndpointId = SBC_SEID.try_into().unwrap();
                assert_eq!(sbc_seid, stream_id);
                responder
                    .send(streams.get(&stream_id).unwrap().endpoint().capabilities().as_slice())
                    .expect("get capabilities response success");
            }
            x => panic!("Expected an avdtp get capabilities request, got {:?}", x),
        };

        match exec.run_until_stalled(&mut get_remote_fut) {
            Poll::Ready(Ok(endpoints)) => assert_eq!(1, endpoints.len()),
            x => panic!("Expected get remote capabilities to be done, got {:?}", x),
        };

        // The second time, it just returns the cached results.
        let get_remote_fut = peer.get_remote_capabilities();
        pin_mut!(get_remote_fut);

        match exec.run_until_stalled(&mut get_remote_fut) {
            Poll::Ready(Ok(endpoints)) => assert_eq!(1, endpoints.len()),
            x => panic!("Expected get remote capabilities to be done, got {:?}", x),
        };
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
