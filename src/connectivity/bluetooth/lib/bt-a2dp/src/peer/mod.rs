// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context,
    bt_a2dp_metrics as metrics,
    bt_avdtp::{
        self as avdtp, MediaCodecType, ServiceCapability, ServiceCategory, StreamEndpoint,
        StreamEndpointId,
    },
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr::{
        ConnectParameters, L2capParameters, ProfileDescriptor, ProfileProxy, PSM_AVDTP,
    },
    fuchsia_bluetooth::{
        inspect::DebugExt,
        types::{Channel, PeerId},
    },
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
    futures::{
        task::{Context as TaskContext, Poll, Waker},
        Future, StreamExt,
    },
    log::{info, trace, warn},
    parking_lot::Mutex,
    std::{
        collections::HashSet,
        convert::TryInto,
        pin::Pin,
        sync::{Arc, Weak},
    },
};

/// For sending out-of-band commands over the A2DP peer.
mod controller;

use crate::stream::{Stream, Streams};
pub use controller::ControllerPool;

/// A Peer represents an A2DP peer which can be connected to this device.
/// A2DP peers are specific to a Bluetooth peer and only one should exist for each unique PeerId.
#[derive(Inspect)]
pub struct Peer {
    /// The id of the peer we are connected to.
    #[inspect(skip)]
    id: PeerId,
    /// Inner keeps track of the peer and the streams.
    #[inspect(forward)]
    inner: Arc<Mutex<PeerInner>>,
    /// Profile Proxy to connect new transport channels
    #[inspect(skip)]
    profile: ProfileProxy,
    /// The profile descriptor for this peer, if it has been discovered.
    #[inspect(skip)]
    descriptor: Mutex<Option<ProfileDescriptor>>,
    /// Wakers that are to be woken when the peer disconnects.  If None, the peers have been woken
    /// and this peer is disconnected.  Shared weakly with ClosedPeer future objects that complete
    /// when the peer disconnects.
    #[inspect(skip)]
    closed_wakers: Arc<Mutex<Option<Vec<Waker>>>>,
    /// Cobalt Sender, if we are sending metrics.
    #[inspect(skip)]
    cobalt_sender: Option<CobaltSender>,
}

impl Peer {
    /// Make a new Peer which is connected to the peer `id` using the AVDTP `peer`.
    /// The `streams` are the local endpoints available to the peer.
    /// `profile` will be used to initiate connections for Media Transport.
    /// If `cobalt_sender` is included, metrics for codec availability will be reported.
    /// This also starts a task on the executor to handle incoming events from the peer.
    pub fn create(
        id: PeerId,
        peer: avdtp::Peer,
        streams: Streams,
        profile: ProfileProxy,
        cobalt_sender: Option<CobaltSender>,
    ) -> Self {
        let res = Self {
            id,
            inner: Arc::new(Mutex::new(PeerInner::new(peer, id, streams))),
            profile,
            descriptor: Mutex::new(None),
            closed_wakers: Arc::new(Mutex::new(Some(Vec::new()))),
            cobalt_sender,
        };
        res.start_requests_task();
        res
    }

    pub fn set_descriptor(&self, descriptor: ProfileDescriptor) -> Option<ProfileDescriptor> {
        self.descriptor.lock().replace(descriptor)
    }

    /// Receive a channel from the peer that was initiated remotely.
    /// This function should be called whenever the peer associated with this opens an L2CAP channel.
    pub fn receive_channel(&self, channel: Channel) -> avdtp::Result<()> {
        let mut lock = self.inner.lock();
        lock.receive_channel(channel)
    }

    /// Return a handle to the AVDTP peer, to use as initiator of commands.
    pub fn avdtp(&self) -> Arc<avdtp::Peer> {
        let lock = self.inner.lock();
        lock.peer.clone()
    }

    /// Returns the stream endpoints discovered by this peer.
    pub fn remote_endpoints(&self) -> Option<Vec<avdtp::StreamEndpoint>> {
        self.inner.lock().remote_endpoints()
    }

    /// Perform Discovery and Collect Capabilities to enumerate the endpoints and capabilities of
    /// the connected peer.
    /// Returns a future which performs the work and resolves to a vector of peer stream endpoints.
    pub fn collect_capabilities(
        &self,
    ) -> impl Future<Output = avdtp::Result<Vec<avdtp::StreamEndpoint>>> {
        let avdtp = self.avdtp();
        let get_all = self.descriptor.lock().map_or(false, a2dp_version_check);
        let inner = self.inner.clone();
        let cobalt_sender = self.cobalt_sender.clone();
        async move {
            if let Some(caps) = inner.lock().remote_endpoints() {
                return Ok(caps);
            }
            trace!("Discovering peer streams..");
            let infos = avdtp.discover().await?;
            trace!("Discovered {} streams", infos.len());
            let mut remote_streams = Vec::new();
            for info in infos {
                let capabilities = if get_all {
                    avdtp.get_all_capabilities(info.id()).await
                } else {
                    avdtp.get_capabilities(info.id()).await
                };
                match capabilities {
                    Ok(capabilities) => {
                        trace!("Stream {:?}", info);
                        for cap in &capabilities {
                            trace!("  - {:?}", cap);
                        }
                        remote_streams.push(avdtp::StreamEndpoint::from_info(&info, capabilities));
                    }
                    Err(e) => {
                        info!("Stream {} capabilities failed: {:?}, skipping", info.id(), e);
                    }
                };
            }
            inner.lock().set_remote_endpoints(&remote_streams);
            if let Some(sender) = cobalt_sender {
                Self::record_cobalt_metrics(sender, &remote_streams);
            }
            Ok(remote_streams)
        }
    }

    fn record_cobalt_metrics(mut cobalt_sender: CobaltSender, endpoints: &[StreamEndpoint]) {
        let codec_metrics: HashSet<_> = endpoints
            .iter()
            .filter_map(|endpoint| {
                endpoint.codec_type().map(|t| codectype_to_availability_metric(t) as u32)
            })
            .collect();
        for metric in codec_metrics {
            cobalt_sender.log_event(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, metric);
        }
    }

    /// Open and start a media transport stream, connecting the local stream `local_id` to the
    /// remote stream `remote_id`, configuring it with the MediaCodec capability.
    /// Returns the MediaStream which can either be streamed from, or an error.
    pub fn stream_start(
        &self,
        local_id: StreamEndpointId,
        remote_id: StreamEndpointId,
        codec_params: ServiceCapability,
    ) -> impl Future<Output = avdtp::Result<()>> {
        let peer = Arc::downgrade(&self.inner);
        let peer_id = self.id.clone();
        let avdtp = self.avdtp();
        let profile = self.profile.clone();

        async move {
            trace!("Starting stream {} to remote {} with {:?}", local_id, remote_id, codec_params);

            let capabilities = vec![ServiceCapability::MediaTransport, codec_params];

            avdtp.set_configuration(&remote_id, &local_id, &capabilities).await?;
            {
                let strong = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                strong.lock().set_opening(&local_id, &remote_id, capabilities.clone())?;
            }
            avdtp.open(&remote_id).await?;

            let channel = profile
                .connect(
                    &mut peer_id.into(),
                    &mut ConnectParameters::L2cap(L2capParameters {
                        psm: Some(PSM_AVDTP),
                        ..L2capParameters::new_empty()
                    }),
                )
                .await
                .context("FIDL error: {}")?
                .or(Err(avdtp::Error::PeerDisconnected))?;
            let channel = match channel.try_into() {
                Err(_e) => {
                    warn!("Couldn't connect media transport {}: no channel", peer_id);
                    return Err(avdtp::Error::PeerDisconnected);
                }
                Ok(c) => c,
            };

            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.receive_channel(channel)?;
            }

            let to_start = &[remote_id];
            avdtp.start(to_start).await?;
            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.start_local_stream(&local_id)
            }
        }
    }

    /// Suspend a media transport stream that connects a local stream `local_id` to the
    /// remote stream `remote_id`.
    /// It's possible that the stream is not active - a suspend will be attempted, but an
    /// error from the command will be returned.
    /// Returns the result of the suspend command.
    pub fn stream_suspend(
        &self,
        local_id: StreamEndpointId,
        remote_id: StreamEndpointId,
    ) -> impl Future<Output = avdtp::Result<()>> {
        let peer = Arc::downgrade(&self.inner);
        let avdtp = self.avdtp();
        async move {
            trace!("Suspending stream {} to remote {}", local_id, remote_id);
            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.suspend_local_stream(&local_id)?;
            }
            let to_suspend = &[remote_id];
            avdtp.suspend(to_suspend).await
        }
    }

    /// Start an asynchronous task to handle any requests from the AVDTP peer.
    /// This task completes when the remote end closes the signaling connection.
    fn start_requests_task(&self) {
        let lock = self.inner.lock();
        let mut request_stream = lock.peer.take_request_stream();
        let id = self.id.clone();
        let peer = Arc::downgrade(&self.inner);
        let disconnect_wakers = Arc::downgrade(&self.closed_wakers);
        fuchsia_async::Task::local(async move {
            while let Some(r) = request_stream.next().await {
                match r {
                    Err(e) => info!("Request Error on {}: {:?}", id, e),
                    Ok(request) => match peer.upgrade() {
                        None => {
                            info!("Peer disappeared processing requests, ending");
                            return;
                        }
                        Some(p) => {
                            let mut lock = p.lock();
                            if let Err(e) = lock.handle_request(request).await {
                                warn!("{} Error handling request: {:?}", id, e);
                            }
                        }
                    },
                }
            }
            info!("Peer {} disconnected", id);
            disconnect_wakers.upgrade().map(|wakers| {
                for waker in wakers.lock().take().unwrap_or_else(Vec::new) {
                    waker.wake();
                }
            });
        })
        .detach();
    }

    /// Returns a future that will complete when the peer disconnects.
    pub fn closed(&self) -> ClosedPeer {
        ClosedPeer { inner: Arc::downgrade(&self.closed_wakers) }
    }
}

/// Future which completes when the A2DP peer has closed the control conection.
/// See `Peer::closed`
pub struct ClosedPeer {
    inner: Weak<Mutex<Option<Vec<Waker>>>>,
}

#[must_use = "futures do nothing unless you `.await` or poll them"]
impl Future for ClosedPeer {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut TaskContext<'_>) -> Poll<Self::Output> {
        match self.inner.upgrade() {
            None => Poll::Ready(()),
            Some(inner) => match inner.lock().as_mut() {
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
    /// The PeerId that this peer is representing
    peer_id: PeerId,
    /// Some(id) if we are opening a StreamEndpoint but haven't finished yet.
    /// This is the local ID.
    /// AVDTP Sec 6.11 - only up to one stream can be in this state.
    opening: Option<StreamEndpointId>,
    /// The local stream endpoint collection
    local: Streams,
    /// The inspect node for this peer
    inspect: fuchsia_inspect::Node,
    /// The set of discovered remote endpoints. None until set.
    remote_endpoints: Option<Vec<StreamEndpoint>>,
    /// The inspect node representing the remote endpoints.
    remote_inspect: fuchsia_inspect::Node,
}

impl Inspect for &mut PeerInner {
    // Set up the StreamEndpoint to update the state
    // The MediaTask node will be created when the media task is started.
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect = parent.create_child(name);
        self.inspect.record_string("id", self.peer_id.to_string());
        self.local.iattach(&self.inspect, "local_streams")
    }
}

impl PeerInner {
    pub fn new(peer: avdtp::Peer, peer_id: PeerId, local: Streams) -> Self {
        Self {
            peer: Arc::new(peer),
            local,
            peer_id,
            opening: None,
            remote_endpoints: None,
            inspect: Default::default(),
            remote_inspect: Default::default(),
        }
    }

    /// Returns an endpoint from the local set or a BadAcpSeid error if it doesn't exist.
    fn get_mut(&mut self, local_id: &StreamEndpointId) -> Result<&mut Stream, avdtp::ErrorCode> {
        self.local.get_mut(&local_id).ok_or(avdtp::ErrorCode::BadAcpSeid)
    }

    fn set_remote_endpoints(&mut self, endpoints: &[StreamEndpoint]) {
        self.remote_inspect = self.inspect.create_child("remote_endpoints");
        for endpoint in endpoints {
            let id_str = endpoint.local_id().to_string();
            let caps_str = endpoint.capabilities().debug();
            self.remote_inspect.record_string(id_str, caps_str);
        }
        self.remote_endpoints = Some(endpoints.iter().map(StreamEndpoint::as_new).collect());
    }

    /// If the remote endpoints have been set, returns a copy of the endpoints.
    fn remote_endpoints(&self) -> Option<Vec<StreamEndpoint>> {
        self.remote_endpoints.as_ref().map(|v| v.iter().map(StreamEndpoint::as_new).collect())
    }

    fn set_opening(
        &mut self,
        local_id: &StreamEndpointId,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        if self.opening.is_some() {
            return Err(avdtp::Error::InvalidState);
        }
        let peer_id = self.peer_id;
        let stream = self.get_mut(&local_id).map_err(|e| avdtp::Error::RequestInvalid(e))?;
        stream
            .configure(&peer_id, &remote_id, capabilities)
            .map_err(|(_, c)| avdtp::Error::RequestInvalid(c))?;
        stream.endpoint_mut().establish().or(Err(avdtp::Error::InvalidState))?;
        self.opening = Some(local_id.clone());
        Ok(())
    }

    fn start_local_stream(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<()> {
        let stream = self.get_mut(&local_id).map_err(|e| avdtp::Error::RequestInvalid(e))?;
        info!("Starting stream: {:?}", stream);
        stream.start().map_err(|c| avdtp::Error::RequestInvalid(c))
    }

    fn suspend_local_stream(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<()> {
        let stream = self.get_mut(&local_id).map_err(|e| avdtp::Error::RequestInvalid(e))?;
        info!("Suspending stream: {:?}", stream);
        stream.suspend().map_err(|c| avdtp::Error::RequestInvalid(c))
    }

    /// Provide a new established L2CAP channel to this remote peer.
    /// This function should be called whenever the remote associated with this peer opens an
    /// L2CAP channel after the first.
    fn receive_channel(&mut self, channel: Channel) -> avdtp::Result<()> {
        let stream_id = self.opening.as_ref().cloned().ok_or(avdtp::Error::InvalidState)?;
        let stream = self.get_mut(&stream_id).map_err(|e| avdtp::Error::RequestInvalid(e))?;
        if !stream.endpoint_mut().receive_channel(channel)? {
            self.opening = None;
        }
        info!("Transport channel connected to seid {}", stream_id);
        Ok(())
    }

    /// Handle a single request event from the avdtp peer.
    async fn handle_request(&mut self, r: avdtp::Request) -> avdtp::Result<()> {
        trace!("Handling {:?} from peer..", r);
        match r {
            avdtp::Request::Discover { responder } => responder.send(&self.local.information()),
            avdtp::Request::GetCapabilities { responder, stream_id }
            | avdtp::Request::GetAllCapabilities { responder, stream_id } => {
                match self.local.get(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => responder.send(stream.endpoint().capabilities()),
                }
            }
            avdtp::Request::Open { responder, stream_id } => {
                if self.opening.is_none() {
                    return responder.reject(avdtp::ErrorCode::BadState);
                }
                let stream = match self.get_mut(&stream_id) {
                    Ok(s) => s,
                    Err(e) => return responder.reject(e),
                };
                match stream.endpoint_mut().establish() {
                    Ok(()) => responder.send(),
                    Err(_) => responder.reject(avdtp::ErrorCode::BadState),
                }
            }
            avdtp::Request::Close { responder, stream_id } => {
                let peer = self.peer.clone();
                match self.get_mut(&stream_id) {
                    Err(e) => responder.reject(e),
                    Ok(stream) => stream.release(responder, &peer).await,
                }
            }
            avdtp::Request::SetConfiguration {
                responder,
                local_stream_id,
                remote_stream_id,
                capabilities,
            } => {
                if self.opening.is_some() {
                    return responder.reject(ServiceCategory::None, avdtp::ErrorCode::BadState);
                }
                self.opening = Some(local_stream_id.clone());
                let peer_id = self.peer_id;
                let stream = match self.get_mut(&local_stream_id) {
                    Err(e) => return responder.reject(ServiceCategory::None, e),
                    Ok(stream) => stream,
                };
                match stream.configure(&peer_id, &remote_stream_id, capabilities) {
                    Ok(_) => responder.send(),
                    Err((category, code)) => responder.reject(category, code),
                }
            }
            avdtp::Request::GetConfiguration { stream_id, responder } => {
                let endpoint = match self.local.get(&stream_id) {
                    None => return responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream.endpoint(),
                };
                match endpoint.get_configuration() {
                    Some(c) => responder.send(&c),
                    // Only happens when the stream is in the wrong state
                    None => responder.reject(avdtp::ErrorCode::BadState),
                }
            }
            avdtp::Request::Reconfigure { responder, local_stream_id, capabilities } => {
                let stream = match self.get_mut(&local_stream_id) {
                    Err(e) => return responder.reject(ServiceCategory::None, e),
                    Ok(stream) => stream,
                };
                match stream.reconfigure(capabilities) {
                    Ok(_) => responder.send(),
                    Err((cat, code)) => responder.reject(cat, code),
                }
            }
            avdtp::Request::Start { responder, stream_ids } => {
                for seid in stream_ids {
                    let stream = match self.get_mut(&seid) {
                        Err(e) => return responder.reject(&seid, e),
                        Ok(stream) => stream,
                    };
                    if let Err(_) = stream.start() {
                        return responder.reject(&seid, avdtp::ErrorCode::BadState);
                    }
                }
                responder.send()
            }
            avdtp::Request::Suspend { responder, stream_ids } => {
                for seid in stream_ids {
                    let stream = match self.get_mut(&seid) {
                        Err(e) => return responder.reject(&seid, e),
                        Ok(stream) => stream,
                    };
                    if let Err(_) = stream.suspend() {
                        return responder.reject(&seid, avdtp::ErrorCode::BadState);
                    }
                }
                responder.send()
            }
            avdtp::Request::Abort { responder, stream_id } => {
                let stream = match self.get_mut(&stream_id) {
                    Err(_) => return Ok(()),
                    Ok(stream) => stream,
                };
                stream.abort(None).await;
                self.opening = self.opening.take().filter(|id| id != &stream_id);
                responder.send()
            }
            avdtp::Request::DelayReport { responder, delay, stream_id } => {
                // Delay is in 1/10 ms
                let delay_ns = delay as u64 * 100000;
                info!(
                    "Peer {} reports stream {} delay of {} ns, acknowledging..",
                    self.peer_id, stream_id, delay_ns
                );
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

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth::ErrorCode;
    use fidl_fuchsia_bluetooth_bredr::{
        ProfileMarker, ProfileRequest, ProfileRequestStream, ServiceClassProfileIdentifier,
    };
    use fidl_fuchsia_cobalt::CobaltEvent;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::pin_mut;
    use matches::assert_matches;
    use std::convert::TryInto;
    use std::task::Poll;

    use crate::media_task::tests::TestMediaTaskBuilder;
    use crate::media_types::*;
    use crate::stream::tests::make_sbc_endpoint;

    fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const BUFFER_SIZE: usize = 100;
        let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
        (CobaltSender::new(sender), receiver)
    }

    fn setup_avdtp_peer() -> (avdtp::Peer, Channel) {
        let (remote, signaling) = Channel::create();
        let peer = avdtp::Peer::new(signaling);
        (peer, remote)
    }

    fn build_test_streams() -> Streams {
        let mut streams = Streams::new();
        let s = Stream::build(make_sbc_endpoint(1), TestMediaTaskBuilder::new().builder());
        streams.insert(s);
        streams
    }

    pub(crate) fn recv_remote(remote: &Channel) -> Result<Vec<u8>, zx::Status> {
        let waiting = remote.as_ref().outstanding_read_bytes();
        assert!(waiting.is_ok());
        let mut response: Vec<u8> = vec![0; waiting.unwrap()];
        let response_read = remote.as_ref().read(response.as_mut_slice())?;
        assert_eq!(response.len(), response_read);
        Ok(response)
    }

    /// Creates a Peer object, returning a channel connected ot the remote end, a
    /// ProfileRequestStream connected to the profile_proxy, and the Peer object.
    fn setup_peer_test() -> (Channel, ProfileRequestStream, mpsc::Receiver<CobaltEvent>, Peer) {
        let (avdtp, remote) = setup_avdtp_peer();
        let (cobalt_sender, cobalt_receiver) = fake_cobalt_sender();
        let (profile_proxy, requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");
        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_test_streams(),
            profile_proxy,
            Some(cobalt_sender),
        );

        (remote, requests, cobalt_receiver, peer)
    }

    fn expect_get_capabilities_and_respond(
        remote: &Channel,
        expected_seid: u8,
        response_capabilities: &[u8],
    ) {
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let mut get_capabilities_rsp = vec![
            txlabel_raw << 4 | 0x2, // TxLabel (same) + ResponseAccept (0x02)
            0x02 // GetCapabilities
        ];

        get_capabilities_rsp.extend_from_slice(response_capabilities);

        assert!(remote.as_ref().write(&get_capabilities_rsp).is_ok());
    }

    fn expect_get_all_capabilities_and_respond(
        remote: &Channel,
        expected_seid: u8,
        response_capabilities: &[u8],
    ) {
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x0C, received[1]); // 0x0C = Get All Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let mut get_capabilities_rsp = vec![
            txlabel_raw << 4 | 0x2, // TxLabel (same) + ResponseAccept (0x02)
            0x0C // GetAllCapabilities
        ];

        get_capabilities_rsp.extend_from_slice(response_capabilities);

        assert!(remote.as_ref().write(&get_capabilities_rsp).is_ok());
    }

    #[test]
    fn test_disconnected() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, _stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("Profile proxy should be created");
        let (remote, signaling) = Channel::create();

        let id = PeerId(1);

        let avdtp = avdtp::Peer::new(signaling);
        let peer = Peer::create(id, avdtp, Streams::new(), proxy, None);

        let closed_fut = peer.closed();

        pin_mut!(closed_fut);

        assert!(exec.run_until_stalled(&mut closed_fut).is_pending());

        // Close the remote channel
        drop(remote);

        assert!(exec.run_until_stalled(&mut closed_fut).is_ready());
    }

    #[test]
    fn test_peer_collect_capabilities_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, _, mut cobalt_receiver, peer) = setup_peer_test();

        let p: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        };
        peer.set_descriptor(p);

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.as_ref().write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 4), Media Type Audio (0x00), Codec type (0x40), Codec specific 0xF09F9296
            0x07, 0x06, 0x00, 0x40, 0xF0, 0x9F, 0x92, 0x96
        ];
        expect_get_capabilities_and_respond(&remote, 0x3E, capabilities_rsp);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 2), Media Type Audio (0x00), Codec type (0x00), Codec specific 0xC0DE
            0x07, 0x04, 0x00, 0x00, 0xC0, 0xDE
        ];
        expect_get_capabilities_and_respond(&remote, 0x01, capabilities_rsp);

        // Should finish!
        match exec.run_until_stalled(&mut collect_future) {
            Poll::Pending => panic!("collect capabilities should be complete"),
            Poll::Ready(Err(e)) => panic!("collect capabilities should have succeeded: {}", e),
            Poll::Ready(Ok(endpoints)) => {
                let first_seid: StreamEndpointId = 0x3E_u8.try_into().unwrap();
                let second_seid: StreamEndpointId = 0x01_u8.try_into().unwrap();
                for stream in endpoints {
                    if stream.local_id() == &first_seid {
                        let expected_caps = vec![
                            ServiceCapability::MediaTransport,
                            ServiceCapability::MediaCodec {
                                media_type: avdtp::MediaType::Audio,
                                codec_type: avdtp::MediaCodecType::new(0x40),
                                codec_extra: vec![0xF0, 0x9F, 0x92, 0x96],
                            },
                        ];
                        assert_eq!(&expected_caps, stream.capabilities());
                    } else if stream.local_id() == &second_seid {
                        let expected_codec_type = avdtp::MediaCodecType::new(0x00);
                        assert_eq!(Some(&expected_codec_type), stream.codec_type());
                    } else {
                        panic!("Unexpected endpoint in the streams collected");
                    }
                }
            }
        }

        // Should have sent two Cobalt events, one for each endpoint.
        let event = cobalt_receiver.try_next().expect("Should get a CobaltEvent").unwrap();
        assert_eq!(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event.metric_id);
        let event = cobalt_receiver.try_next().expect("Should get a second CobaltEvent").unwrap();
        assert_eq!(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event.metric_id);

        // The second time, we don't expect to ask the peer again.
        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        match exec.run_until_stalled(&mut collect_future) {
            Poll::Ready(Ok(endpoints)) => assert_eq!(2, endpoints.len()),
            x => panic!("Expected get remote capabilities to be done, got {:?}", x),
        };

        // Shouldn't report any more to cobalt since we already did so.
        assert!(cobalt_receiver.try_next().is_err());
    }

    #[test]
    fn test_peer_collect_all_capabilities_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, _, mut cobalt_receiver, peer) = setup_peer_test();
        let p: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        peer.set_descriptor(p);

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.as_ref().write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get all capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 4), Media Type Audio (0x00), Codec type (0x40), Codec specific 0xF09F9296
            0x07, 0x06, 0x00, 0x40, 0xF0, 0x9F, 0x92, 0x96
        ];
        expect_get_all_capabilities_and_respond(&remote, 0x3E, capabilities_rsp);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get all capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 2), Media Type Audio (0x00), Codec type (0x00), Codec specific 0xC0DE
            0x07, 0x04, 0x00, 0x00, 0xC0, 0xDE
        ];
        expect_get_all_capabilities_and_respond(&remote, 0x01, capabilities_rsp);

        // Should finish!
        match exec.run_until_stalled(&mut collect_future) {
            Poll::Pending => panic!("collect capabilities should be complete"),
            Poll::Ready(Err(e)) => panic!("collect capabilities should have succeeded: {}", e),
            Poll::Ready(Ok(endpoints)) => {
                let first_seid: StreamEndpointId = 0x3E_u8.try_into().unwrap();
                let second_seid: StreamEndpointId = 0x01_u8.try_into().unwrap();
                for stream in endpoints {
                    if stream.local_id() == &first_seid {
                        let expected_caps = vec![
                            ServiceCapability::MediaTransport,
                            ServiceCapability::MediaCodec {
                                media_type: avdtp::MediaType::Audio,
                                codec_type: avdtp::MediaCodecType::new(0x40),
                                codec_extra: vec![0xF0, 0x9F, 0x92, 0x96],
                            },
                        ];
                        assert_eq!(&expected_caps, stream.capabilities());
                    } else if stream.local_id() == &second_seid {
                        let expected_codec_type = avdtp::MediaCodecType::new(0x00);
                        assert_eq!(Some(&expected_codec_type), stream.codec_type());
                    } else {
                        panic!("Unexpected endpoint in the streams collected");
                    }
                }
            }
        }

        // Should have sent two Cobalt events, one for each endpoint.
        let event = cobalt_receiver.try_next().expect("Should get a CobaltEvent").unwrap();
        assert_eq!(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event.metric_id);
        let event = cobalt_receiver.try_next().expect("Should get a second CobaltEvent").unwrap();
        assert_eq!(metrics::A2DP_CODEC_AVAILABILITY_METRIC_ID, event.metric_id);

        // The second time, we don't expect to ask the peer again.
        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        match exec.run_until_stalled(&mut collect_future) {
            Poll::Ready(Ok(endpoints)) => assert_eq!(2, endpoints.len()),
            x => panic!("Expected get remote capabilities to be done, got {:?}", x),
        };

        // Shouldn't report any more to cobalt since we already did so.
        assert!(cobalt_receiver.try_next().is_err());
    }

    #[test]
    fn test_peer_collect_capabilities_discovery_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, _, _, peer) = setup_peer_test();

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        // Shouldn't finish yet.
        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with an eror.
        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x01,                         // Discover
            0x31,                         // BAD_STATE
        ];
        assert!(remote.as_ref().write(response).is_ok());

        // Should be done with an error.
        // Should finish!
        match exec.run_until_stalled(&mut collect_future) {
            Poll::Pending => panic!("Should be ready after discovery failure"),
            Poll::Ready(Ok(x)) => panic!("Should be an error but returned {:?}", x),
            Poll::Ready(Err(e)) => assert_matches!(e, avdtp::Error::RemoteRejected(0x31)),
        }
    }

    #[test]
    fn test_peer_collect_capabilities_get_capability_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, _, _, peer) = setup_peer_test();

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        // Shouldn't finish yet.
        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.as_ref().write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities request
        let expected_seid = 0x3E;
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x02,                         // Get Capabilities
            0x12,                         // BAD_ACP_SEID
        ];
        assert!(remote.as_ref().write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities request (skipped the last one)
        let expected_seid = 0x01;
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x02,                         // Get Capabilities
            0x12,                         // BAD_ACP_SEID
        ];
        assert!(remote.as_ref().write(response).is_ok());

        // Should be done without an error, but with no streams.
        match exec.run_until_stalled(&mut collect_future) {
            Poll::Pending => panic!("Should be ready after discovery failure"),
            Poll::Ready(Err(e)) => panic!("Shouldn't be an error but returned {:?}", e),
            Poll::Ready(Ok(map)) => assert_eq!(0, map.len()),
        }
    }

    fn receive_simple_accept(remote: &Channel, signal_id: u8) {
        let received = recv_remote(&remote).expect("expected a packet");
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(signal_id, received[1]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            signal_id,
        ];
        assert!(remote.as_ref().write(response).is_ok());
    }

    #[test]
    fn test_peer_stream_start_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, mut profile_request_stream, _, peer) = setup_peer_test();

        // This needs to match the test stream.
        let local_seid = 1_u8.try_into().unwrap();
        let remote_seid = 2_u8.try_into().unwrap();

        let codec_params = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x11, 0x45, 51, 51],
        };

        let start_future = peer.stream_start(local_seid, remote_seid, codec_params);
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

        // Should connect the media channel after open.
        let (_, transport) = Channel::create();

        let request = exec.run_until_stalled(&mut profile_request_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.into());
                responder.send(&mut Ok(transport.into())).expect("responder sends");
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
        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => panic!("Should be ready after start succeeds"),
            Poll::Ready(Err(e)) => panic!("Shouldn't be an error but returned {:?}", e),
            // TODO: confirm the stream is usable
            Poll::Ready(Ok(_stream)) => {}
        }
    }

    #[test]
    fn test_peer_stream_start_fails_to_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (remote, mut profile_request_stream, _, peer) = setup_peer_test();

        // This needs to match the local stream id.
        let local_seid = 1_u8.try_into().unwrap();
        let remote_seid = 2_u8.try_into().unwrap();

        let codec_params = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x11, 0x45, 51, 51],
        };

        let start_future = peer.stream_start(local_seid, remote_seid, codec_params);
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

        // Should connect the media channel after open.
        let request = exec.run_until_stalled(&mut profile_request_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::Connect { peer_id, responder, .. }))) => {
                assert_eq!(PeerId(1), peer_id.into());
                responder.send(&mut Err(ErrorCode::Failed)).expect("responder sends");
            }
            x => panic!("Should have sent a open l2cap request, but got {:?}", x),
        };

        // Should return an error.
        // Should be done without an error, but with no streams.
        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => panic!("Should be ready after start fails"),
            Poll::Ready(Ok(_stream)) => panic!("Shouldn't have succeeded stream here"),
            Poll::Ready(Err(_)) => {}
        }
    }

    /// Test that the delay reports get acknowledged
    #[test]
    fn test_peer_delay_report() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();
        let (profile_proxy, _requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");
        let mut streams = Streams::new();
        let test_builder = TestMediaTaskBuilder::new();
        streams.insert(Stream::build(make_sbc_endpoint(1), test_builder.builder()));

        let _peer = Peer::create(PeerId(1), avdtp, streams, profile_proxy, None);
        let remote_peer = avdtp::Peer::new(remote);

        // Stream ID chosen randomly by fair dice roll
        let seid: StreamEndpointId = 0x09_u8.try_into().unwrap();
        let delay_report_fut = remote_peer.delay_report(&seid, 0xc0de);
        pin_mut!(delay_report_fut);

        match exec.run_until_stalled(&mut delay_report_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected delay report to complete and got {:?}", x),
        };
    }

    /// Test that the remote end can configure and start a stream.
    #[test]
    fn test_peer_as_acceptor() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();
        let (profile_proxy, _requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");
        let mut streams = Streams::new();
        let mut test_builder = TestMediaTaskBuilder::new();
        streams.insert(Stream::build(make_sbc_endpoint(1), test_builder.builder()));
        let next_task_fut = test_builder.next_task();
        pin_mut!(next_task_fut);

        let peer = Peer::create(PeerId(1), avdtp, streams, profile_proxy, None);
        let remote_peer = avdtp::Peer::new(remote);

        let discover_fut = remote_peer.discover();
        pin_mut!(discover_fut);

        let expected = vec![make_sbc_endpoint(1).information()];
        match exec.run_until_stalled(&mut discover_fut) {
            Poll::Ready(Ok(res)) => assert_eq!(res, expected),
            x => panic!("Expected discovery to complete and got {:?}", x),
        };

        let sbc_endpoint_id = 1_u8.try_into().expect("should be able to get sbc endpointid");
        let unknown_endpoint_id = 2_u8.try_into().expect("should be able to get sbc endpointid");

        let get_caps_fut = remote_peer.get_capabilities(&sbc_endpoint_id);
        pin_mut!(get_caps_fut);

        match exec.run_until_stalled(&mut get_caps_fut) {
            // There are two caps (mediatransport, mediacodec) in the sbc endpoint.
            Poll::Ready(Ok(caps)) => assert_eq!(2, caps.len()),
            x => panic!("Get capabilities should be ready but got {:?}", x),
        };

        let get_caps_fut = remote_peer.get_capabilities(&unknown_endpoint_id);
        pin_mut!(get_caps_fut);

        match exec.run_until_stalled(&mut get_caps_fut) {
            // 0x12 is BadAcpSeid
            Poll::Ready(Err(avdtp::Error::RemoteRejected(0x12))) => {}
            x => panic!("Get capabilities should be a ready error but got {:?}", x),
        };

        let get_caps_fut = remote_peer.get_all_capabilities(&sbc_endpoint_id);
        pin_mut!(get_caps_fut);

        match exec.run_until_stalled(&mut get_caps_fut) {
            // There are two caps (mediatransport, mediacodec) in the sbc endpoint.
            Poll::Ready(Ok(caps)) => assert_eq!(2, caps.len()),
            x => panic!("Get capabilities should be ready but got {:?}", x),
        };

        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::SIXTEEN,
            SbcSubBands::EIGHT,
            SbcAllocation::LOUDNESS,
            /* min_bpv= */ 53,
            /* max_bpv= */ 53,
        )
        .expect("sbc codec info");

        let capabilities = vec![
            avdtp::ServiceCapability::MediaTransport,
            avdtp::ServiceCapability::MediaCodec {
                media_type: avdtp::MediaType::Audio,
                codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                codec_extra: sbc_codec_info.to_bytes().to_vec(),
            },
        ];

        let set_config_fut =
            remote_peer.set_configuration(&sbc_endpoint_id, &sbc_endpoint_id, &capabilities);
        pin_mut!(set_config_fut);

        match exec.run_until_stalled(&mut set_config_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Set capabilities should be ready but got {:?}", x),
        };

        // The task should be created locally
        let media_task = match exec.run_until_stalled(&mut next_task_fut) {
            Poll::Ready(Some(task)) => task,
            x => panic!("Local task should be created at this point: {:?}", x),
        };
        assert!(!media_task.is_started());

        let open_fut = remote_peer.open(&sbc_endpoint_id);
        pin_mut!(open_fut);
        match exec.run_until_stalled(&mut open_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Open should be ready but got {:?}", x),
        };

        // Establish a media transport stream
        let (_remote_transport, transport) = Channel::create();

        assert_eq!(Some(()), peer.receive_channel(transport).ok());

        let stream_ids = vec![sbc_endpoint_id.clone()];
        let start_fut = remote_peer.start(&stream_ids);
        pin_mut!(start_fut);
        match exec.run_until_stalled(&mut start_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Start should be ready but got {:?}", x),
        };

        // Should have started the media task
        assert!(media_task.is_started());

        let suspend_fut = remote_peer.suspend(&stream_ids);
        pin_mut!(suspend_fut);
        match exec.run_until_stalled(&mut suspend_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Start should be ready but got {:?}", x),
        };

        // Should have stopped the media task on suspend.
        assert!(!media_task.is_started());
    }

    /// Test that the version check method correctly differentiates between newer
    /// and older A2DP versions.
    #[test]
    fn test_a2dp_version_check() {
        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        assert_eq!(true, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 10,
        };
        assert_eq!(true, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 0,
        };
        assert_eq!(false, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 0,
            minor_version: 9,
        };
        assert_eq!(false, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 2,
        };
        assert_eq!(true, a2dp_version_check(p1));
    }
}
