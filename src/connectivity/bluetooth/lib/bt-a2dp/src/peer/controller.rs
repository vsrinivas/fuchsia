// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_avdtp::{Error, MediaCodecType, MediaType, ServiceCapability, StreamEndpointId},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_avdtp::{
        PeerControllerMarker, PeerControllerRequest, PeerControllerRequestStream, PeerError,
        PeerManagerControlHandle, PeerManagerRequest, PeerManagerRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::{detachable_map::DetachableWeak, types::PeerId},
    futures::{TryFutureExt, TryStreamExt},
    log::{error, info, warn},
    parking_lot::Mutex,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

use crate::peer::Peer;

/// ID for the SBC stream endpoint. This is the same SEID that is chosen
/// in A2DP Sink and Source.
const SBC_SEID: u8 = 6;

/// A structure that handles requests from peer controller, specific to a peer.
struct Controller {}

impl Controller {
    /// Handle one request from the PeerController and respond with the results from sending the
    /// command(s) to the peer
    async fn handle_controller_request(
        a2dp: Arc<Peer>,
        request: PeerControllerRequest,
        endpoint_id: &StreamEndpointId,
    ) -> Result<(), fidl::Error> {
        match request {
            PeerControllerRequest::SetConfiguration { responder } => {
                let generic_capabilities = vec![ServiceCapability::MediaTransport];
                match a2dp
                    .avdtp()
                    .set_configuration(endpoint_id, endpoint_id, &generic_capabilities)
                    .await
                {
                    Ok(resp) => {
                        info!("SetConfiguration successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("SetConfiguration for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetConfiguration { responder } => {
                match a2dp.avdtp().get_configuration(endpoint_id).await {
                    Ok(service_capabilities) => {
                        info!(
                            "Service capabilities from GetConfiguration: {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("GetConfiguration for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetCapabilities { responder } => {
                match a2dp.avdtp().get_capabilities(endpoint_id).await {
                    Ok(service_capabilities) => {
                        info!(
                            "Service capabilities from GetCapabilities {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("GetCapabilities for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetAllCapabilities { responder } => {
                match a2dp.avdtp().get_all_capabilities(endpoint_id).await {
                    Ok(service_capabilities) => {
                        info!(
                            "Service capabilities from GetAllCapabilities: {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("GetAllCapabilities for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendStream { responder } => {
                let local_id: StreamEndpointId = SBC_SEID.try_into().expect("should work");
                match a2dp.stream_suspend(local_id, endpoint_id.clone()).await {
                    Ok(_) => {
                        info!("SuspendStream was successful");
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("SuspendStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::ReconfigureStream { responder } => {
                // Only one frequency, channel mode, block length, subband,
                // and allocation for reconfigure (A2DP 4.3.2)
                let generic_capabilities = vec![ServiceCapability::MediaCodec {
                    media_type: MediaType::Audio,
                    codec_type: MediaCodecType::AUDIO_SBC,
                    codec_extra: vec![0x11, 0x15, 2, 250],
                }];
                match a2dp.avdtp().reconfigure(endpoint_id, &generic_capabilities[..]).await {
                    Ok(resp) => {
                        info!("ReconfigureStream was successful {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("ReconfigureStream for {} failed: {:?}", endpoint_id, e);
                        match e {
                            Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
                    }
                }
            }
            PeerControllerRequest::ReleaseStream { responder } => {
                match a2dp.avdtp().close(endpoint_id).await {
                    Ok(resp) => {
                        info!("ReleaseStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("ReleaseStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::StartStream { responder } => {
                match a2dp.avdtp().start(&[endpoint_id.clone()]).await {
                    Ok(resp) => {
                        info!("StartStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        error!("StartStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::AbortStream { responder } => {
                match a2dp.avdtp().abort(endpoint_id).await {
                    Ok(resp) => {
                        info!("AbortStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        info!("AbortStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::EstablishStream { responder } => {
                match a2dp.avdtp().open(endpoint_id).await {
                    Ok(resp) => {
                        info!("EstablishStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        info!("EstablishStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendAndReconfigure { responder } => {
                let local_id: StreamEndpointId = SBC_SEID.try_into().expect("should work");
                match a2dp.stream_suspend(local_id, endpoint_id.clone()).await {
                    Ok(_) => {
                        info!("SuspendStream was successful");
                        // Only one frequency, channel mode, block length, subband,
                        // and allocation for reconfigure (A2DP 4.3.2)
                        let generic_capabilities = vec![ServiceCapability::MediaCodec {
                            media_type: MediaType::Audio,
                            codec_type: MediaCodecType::AUDIO_SBC,
                            codec_extra: vec![0x11, 0x15, 2, 250],
                        }];
                        match a2dp.avdtp().reconfigure(endpoint_id, &generic_capabilities[..]).await
                        {
                            Ok(resp) => {
                                info!("ReconfigureStream was successful {:?}", resp);
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                info!("ReconfigureStream for {} failed: {:?}", endpoint_id, e);
                                responder.send(&mut Err(PeerError::ProtocolError))?;
                            }
                        }
                    }
                    Err(e) => {
                        info!("SuspendStream for {} failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Process all the requests from a peer controller.
    /// Finishes when an error occurs on the stream or the stream is closed (the controller
    /// disconnects).  Errors that happen within each specific request are logged but do not close
    /// the stream.
    async fn process_requests(
        mut stream: PeerControllerRequestStream,
        peer: DetachableWeak<PeerId, Peer>,
        peer_id: PeerId,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            let peer = { peer.upgrade().ok_or(format_err!("Peer disconnected"))? };
            // Find the first discovered stream by the A2DP peer.
            let infos = match peer.remote_endpoints() {
                Some(endpoints) => endpoints,
                None => {
                    info!("No streams exist on the peer");
                    continue;
                }
            };
            let remote_id = match infos.first() {
                Some(stream_info) => stream_info.local_id().clone(),
                None => {
                    info!("Can't execute {:?} - no streams exist on the peer.", req);
                    continue;
                }
            };

            let fut = Self::handle_controller_request(peer.clone(), req, &remote_id);
            if let Err(e) = fut.await {
                error!("{} error handling request: {:?}", peer_id, e);
            }
        }

        info!("Controller finished for id: {}", peer_id);
        Ok(())
    }
}

/// Control implementation to handle FIDL requests.
/// State is stored in the remotes object.
async fn start_control_service(
    mut stream: PeerManagerRequestStream,
    controller_pool: Arc<Mutex<ControllerPoolInner>>,
) -> Result<(), anyhow::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerRequest::GetPeer { peer_id, handle, .. } => {
                let handle_to_client: fidl::endpoints::ServerEnd<PeerControllerMarker> = handle;
                let peer_id: PeerId = peer_id.into();
                let peer = match controller_pool.lock().get_peer(&peer_id) {
                    None => {
                        info!("GetPeer: request for nonexistent peer {}, closing.", peer_id);
                        // Dropping the handle will close the connection.
                        continue;
                    }
                    Some(peer) => peer.clone(),
                };
                info!("GetPeer: Creating peer controller for peer with id {}.", peer_id);
                match handle_to_client.into_stream() {
                    Err(err) => {
                        warn!("Error. Unable to create server endpoint from stream: {:?}.", err);
                    }
                    Ok(client_stream) => fasync::Task::local(async move {
                        Controller::process_requests(client_stream, peer, peer_id)
                            .await
                            .unwrap_or_else(|e| error!("Requests failed: {:?}", e))
                    })
                    .detach(),
                };
            }
            PeerManagerRequest::ConnectedPeers { responder } => {
                let mut connected_peers: Vec<fidl_fuchsia_bluetooth::PeerId> =
                    controller_pool.lock().connected_peers().into_iter().map(Into::into).collect();
                responder.send(&mut connected_peers.iter_mut())?;
                info!("ConnectedPeers request. Peers: {:?}", connected_peers);
            }
        }
    }
    Ok(())
}

/// A pool of peers which can be potentially controlled by a connected PeerManager protocol.
/// Each peer will persist in `peers` until both the peer is disconnected and a command fails.
struct ControllerPoolInner {
    peers: HashMap<PeerId, DetachableWeak<PeerId, Peer>>,
    control_handle: Option<PeerManagerControlHandle>,
}

impl ControllerPoolInner {
    pub fn new() -> Self {
        Self { control_handle: None, peers: HashMap::new() }
    }

    #[cfg(test)]
    fn control_handle(&self) -> Option<PeerManagerControlHandle> {
        self.control_handle.clone()
    }

    /// Returns a list of the currently connected peers.
    fn connected_peers(&self) -> Vec<PeerId> {
        self.peers.keys().cloned().collect()
    }

    /// Returns a DetachableWeak reference to the A2DP peer, should it exist.
    fn get_peer(&self, id: &PeerId) -> Option<&DetachableWeak<PeerId, Peer>> {
        self.peers.get(id)
    }

    /// Sets the control_handle. Returns true if set, false otherwise.
    fn set_control_handle(&mut self, control_handle: PeerManagerControlHandle) -> bool {
        if self.control_handle.is_none() {
            self.control_handle = Some(control_handle);
            return true;
        }
        false
    }

    /// Inserts a weak reference to the A2DP peer and notifies the control handle of
    /// the connection.
    fn peer_connected(&mut self, peer_id: PeerId, peer: DetachableWeak<PeerId, Peer>) {
        self.peers.insert(peer_id, peer);
        if let Some(handle) = self.control_handle.as_ref() {
            if let Err(e) = handle.send_on_peer_connected(&mut peer_id.into()) {
                info!("Peer connected callback failed: {:?}", e);
            }
        }
    }
}

pub struct ControllerPool {
    inner: Arc<Mutex<ControllerPoolInner>>,
}

impl ControllerPool {
    pub fn new() -> Self {
        Self { inner: Arc::new(Mutex::new(ControllerPoolInner::new())) }
    }

    /// Returns the control_handle associated with this controller, if set.
    #[cfg(test)]
    fn control_handle(&self) -> Option<PeerManagerControlHandle> {
        self.inner.lock().control_handle()
    }

    /// Returns a cloned copy of of the A2DP peer, if it exists.
    #[cfg(test)]
    fn get_peer(&self, id: &PeerId) -> Option<DetachableWeak<PeerId, Peer>> {
        self.inner.lock().get_peer(id).cloned()
    }

    /// Called when a client connects to the PeerManager service. Stores the control_handle
    /// associated with the request stream and spawns a task to handle incoming requests.
    ///
    /// There can only be one active client at a time. As such, any client connections thereafter
    /// will be dropped.
    pub fn connected(&self, stream: PeerManagerRequestStream) {
        if self.inner.lock().set_control_handle(stream.control_handle().clone()) {
            // Spawns the control service task if the control handle hasn't been set.
            let inner = self.inner.clone();
            fasync::Task::local(
                start_control_service(stream, inner)
                    .unwrap_or_else(|e| error!("Error handling requests {:?}", e)),
            )
            .detach()
        }
    }

    /// Stores the weak reference to the A2DP peer and notifies the control handle of the connection.
    /// This should be called once for every connected remote peer.
    pub fn peer_connected(&self, peer_id: PeerId, peer: DetachableWeak<PeerId, Peer>) {
        self.inner.lock().peer_connected(peer_id, peer);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bt_avdtp::{EndpointType, ErrorCode, Peer as AvdtpPeer, Request, StreamInformation};
    use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_avdtp::*;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::{detachable_map::DetachableMap, types::Channel};
    use futures::{self, StreamExt};
    use std::convert::TryFrom;

    use crate::media_task::tests::TestMediaTaskBuilder;
    use crate::stream::{tests::make_sbc_endpoint, Stream, Streams};

    /// Reads from the AVDTP request stream, sending back acknowledgements, unless otherwise noted.
    async fn listen_for_avdtp_requests(remote: Channel) {
        let remote_avdtp = AvdtpPeer::new(remote);
        let mut remote_requests = remote_avdtp.take_request_stream();
        while let Some(request) = remote_requests.next().await {
            match request {
                Ok(Request::Discover { responder }) => {
                    let endpoint_id = StreamEndpointId::try_from(1).expect("endpoint id creation");
                    let information = StreamInformation::new(
                        endpoint_id,
                        false,
                        MediaType::Audio,
                        EndpointType::Source,
                    );
                    responder.send(&[information]).expect("Sending response should have worked");
                }
                Ok(Request::GetCapabilities { responder, .. })
                | Ok(Request::GetAllCapabilities { responder, .. })
                | Ok(Request::GetConfiguration { responder, .. }) => {
                    responder.send(&[]).expect("Sending response should have worked");
                }
                Ok(Request::SetConfiguration { responder, .. })
                | Ok(Request::Reconfigure { responder, .. }) => {
                    responder.send().expect("Sending response should have worked");
                }
                Ok(Request::Suspend { responder, .. }) | Ok(Request::Start { responder, .. }) => {
                    responder.send().expect("Sending response should have worked");
                }
                Ok(Request::Open { responder, .. })
                | Ok(Request::Close { responder, .. })
                | Ok(Request::Abort { responder, .. }) => {
                    // Purposefully make this fail, to ensure fail condition branch works.
                    responder
                        .reject(ErrorCode::UnsupportedConfiguration)
                        .expect("Sending response should have worked");
                }
                _ => {
                    panic!("Got an unhandled AVDTP request");
                }
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests when a client connects to the PeerManager, a listening task is spawned,
    /// and requests can be served.
    /// Note: This test does not test the correctness of the underlying AVDTP commands. The AVDTP
    /// commands should be well tested in `bluetooth/lib/avdtp`.
    async fn test_client_connected_to_peer_manager() {
        // Create the ControllerPool. This stores all active peers and handles listening
        // to PeerManager and PeerController requests.
        let (pm_proxy, pm_stream) = create_proxy_and_stream::<PeerManagerMarker>().unwrap();
        let controller_pool = ControllerPool::new();
        let mut peer_map = DetachableMap::new();

        // Send a `connected` signal to simulate client connection.
        controller_pool.connected(pm_stream);

        // Create a fake peer, and simulate connection by sending the `peer_connected` signal.
        let fake_peer_id = PeerId(12345);
        let (profile_proxy, _requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");
        let (remote, signaling) = Channel::create();
        let avdtp_peer = AvdtpPeer::new(signaling);
        let mut streams = Streams::new();
        let test_builder = TestMediaTaskBuilder::new();
        streams.insert(Stream::build(make_sbc_endpoint(1), test_builder.builder()));
        let peer = Peer::create(fake_peer_id, avdtp_peer, streams, profile_proxy, None);
        peer_map.insert(fake_peer_id, peer);
        let weak_peer = peer_map.get(&fake_peer_id).expect("just inserted");

        controller_pool.peer_connected(fake_peer_id, weak_peer);
        assert!(controller_pool.control_handle().is_some());
        assert!(controller_pool.get_peer(&fake_peer_id).is_some());

        // Client connects to controller by sending `get_peer`.
        let (client, server) =
            create_endpoints::<PeerControllerMarker>().expect("Couldn't create peer endpoint");
        let client_proxy = client.into_proxy().expect("Couldn't obtain client proxy");
        let res = pm_proxy.get_peer(&mut fake_peer_id.into(), server);
        assert_eq!(Ok(()), res.map_err(|e| e.to_string()));

        // Spawn task that acts as remote end and replies with simple responses.
        fasync::Task::spawn(listen_for_avdtp_requests(remote)).detach();

        // Discover the streams of the remote peer.
        {
            let strong =
                peer_map.get(&fake_peer_id).and_then(|p| p.upgrade()).expect("should be connected");
            let cap_fut = strong.collect_capabilities();
            assert!(cap_fut.await.is_ok());
        }

        // Send client commands over the proxy, and make sure responses are expected.
        let res = client_proxy.set_configuration().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_configuration().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_capabilities().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_all_capabilities().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        // Suspend command should work, but suspending the stream will fail
        // since there is no active stream.
        let res = client_proxy.suspend_stream().await;
        assert_eq!(Ok(Err(PeerError::ProtocolError)), res.map_err(|e| e.to_string()));

        let res = client_proxy.reconfigure_stream().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        // Suspend command should work, but suspending the stream will fail
        // since there is no active stream.
        let res = client_proxy.suspend_and_reconfigure().await;
        assert_eq!(Ok(Err(PeerError::ProtocolError)), res.map_err(|e| e.to_string()));

        let res = client_proxy.start_stream().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        // FIDL method should work, but the underlying AVDTP method call should return an error.
        // See `listen_for_avdtp_requests`.
        let res = client_proxy.release_stream().await.expect("Command should succeed");
        assert_eq!(Err("ProtocolError".to_string()), res.map_err(|e| format!("{:?}", e)));

        // FIDL method should work, but the underlying AVDTP method call should return an error.
        // See `listen_for_avdtp_requests`.
        let res = client_proxy.establish_stream().await.expect("Command should succeed");
        assert_eq!(Err("ProtocolError".to_string()), res.map_err(|e| format!("{:?}", e)));

        // FIDL method should work, but the underlying AVDTP method call should return an error.
        // See `listen_for_avdtp_requests`.
        let res = client_proxy.abort_stream().await.expect("Command should succeed");
        assert_eq!(Err("ProtocolError".to_string()), res.map_err(|e| format!("{:?}", e)));
    }
}
