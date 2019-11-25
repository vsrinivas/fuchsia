// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp::{self as avdtp, StreamEndpoint, StreamEndpointId},
    fidl_fuchsia_bluetooth_bredr::ProfileDescriptor,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
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
    /// The profile descriptor for this peer, if it has been discovered.
    descriptor: Option<ProfileDescriptor>,
}

impl Peer {
    /// Make a new Peer which is connected to the peer `id` using the AVDTP `peer`.
    /// The `streams` are the local endpoints available to the peer.
    /// The `inspect` is the inspect node associated with the peer.
    pub fn create(id: PeerId, peer: avdtp::Peer, streams: Streams, inspect: inspect::Node) -> Self {
        let res = Self {
            id,
            inner: Arc::new(Mutex::new(PeerInner::new(peer, streams, inspect))),
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

    /// Perform Discovery and then Capability detection to discover the capabilities of the
    /// connected peer's stream endpoints
    /// Returns a map of remote stream endpoint ids associated with the MediaCodec serviice capability of
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
}

impl PeerInner {
    fn new(peer: avdtp::Peer, mut streams: Streams, inspect: inspect::Node) -> Self {
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
        }
    }

    /// Returns an endpoint from the local set or a BadAcpSeid error if it doesn't exist.
    fn get_mut(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<&mut StreamEndpoint> {
        self.local
            .get_endpoint(&local_id)
            .ok_or(avdtp::Error::RequestInvalid(avdtp::ErrorCode::BadAcpSeid))
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
                    let inspect = &mut self.inspect;
                    let res = self.local.get_mut(&seid).and_then(|stream| {
                        let inspect = inspect.create_streaming_inspect_data(&seid);
                        stream.start(inspect).or(Err(avdtp::ErrorCode::BadState))
                    });
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
    use fidl_fuchsia_bluetooth_bredr::ServiceClassProfileIdentifier;
    use futures::pin_mut;

    #[test]
    fn test_disconnected() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        let (remote, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        let id = PeerId(1);

        let avdtp = avdtp::Peer::new(signaling).expect("peer should be creatable");
        let inspect = inspect::Inspector::new();
        let inspect = inspect.root().create_child(format!("peer {}", id));

        let peer = Peer::create(id, avdtp, Streams::new(), inspect);

        let closed_fut = peer.closed();

        pin_mut!(closed_fut);

        assert!(exec.run_until_stalled(&mut closed_fut).is_pending());

        // Close the remote socket.
        drop(remote);

        assert!(exec.run_until_stalled(&mut closed_fut).is_ready());
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
