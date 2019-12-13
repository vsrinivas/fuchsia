// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_avdtp::{
        PeerControllerMarker, PeerControllerRequest, PeerControllerRequestStream, PeerError,
        PeerManagerControlHandle, PeerManagerRequest, PeerManagerRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn},
    futures::{TryFutureExt, TryStreamExt},
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

use crate::{
    types::{Error, MediaCodecType, MediaType, ServiceCapability, StreamEndpointId},
    Peer,
};

/// A structure that handles a requests from peer controller, specific to a peer.
struct AvdtpController {}

impl AvdtpController {
    /// Handle one request from the PeerController and respond with the results from sending the
    /// comamnd(s) to the peer
    async fn handle_controller_request(
        avdtp: Arc<Peer>,
        request: PeerControllerRequest,
        endpoint_id: &StreamEndpointId,
    ) -> Result<(), fidl::Error> {
        match request {
            PeerControllerRequest::SetConfiguration { responder } => {
                let generic_capabilities = vec![ServiceCapability::MediaTransport];
                match avdtp.set_configuration(endpoint_id, endpoint_id, &generic_capabilities).await
                {
                    Ok(resp) => {
                        fx_log_info!("SetConfiguration successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} set_configuration failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetConfiguration { responder } => {
                match avdtp.get_configuration(endpoint_id).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetConfiguration: {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_configuration failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetCapabilities { responder } => {
                match avdtp.get_capabilities(endpoint_id).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetCapabilities {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_capabilities failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::GetAllCapabilities { responder } => {
                match avdtp.get_all_capabilities(endpoint_id).await {
                    Ok(service_capabilities) => {
                        fx_log_info!(
                            "Service capabilities from GetAllCapabilities: {:?}",
                            service_capabilities
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} get_all_capabilities failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendStream { responder } => {
                let suspend_vec = [endpoint_id.clone()];
                match avdtp.suspend(&suspend_vec[..]).await {
                    Ok(resp) => {
                        fx_log_info!("SuspendStream was successful {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} suspend failed: {:?}", endpoint_id, e);
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
                match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                    Ok(resp) => {
                        fx_log_info!("Reconfigure was successful {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} reconfigure failed: {:?}", endpoint_id, e);
                        match e {
                            Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
                    }
                }
            }
            PeerControllerRequest::ReleaseStream { responder } => {
                match avdtp.close(endpoint_id).await {
                    Ok(resp) => {
                        fx_log_info!("ReleaseStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} release failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::StartStream { responder } => {
                match avdtp.start(&[endpoint_id.clone()]).await {
                    Ok(resp) => {
                        fx_log_info!("StartStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} start failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::AbortStream { responder } => {
                match avdtp.abort(endpoint_id).await {
                    Ok(resp) => {
                        fx_log_info!("Abort was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} abort failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::EstablishStream { responder } => {
                match avdtp.open(endpoint_id).await {
                    Ok(resp) => {
                        fx_log_info!("EstablishStream was successful: {:?}", resp);
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} establish failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::SuspendAndReconfigure { responder } => {
                let suspend_vec = [endpoint_id.clone()];
                match avdtp.suspend(&suspend_vec[..]).await {
                    Ok(resp) => {
                        fx_log_info!("Suspend was successful {:?}", resp);
                        // Only one frequency, channel mode, block length, subband,
                        // and allocation for reconfigure (A2DP 4.3.2)
                        let generic_capabilities = vec![ServiceCapability::MediaCodec {
                            media_type: MediaType::Audio,
                            codec_type: MediaCodecType::AUDIO_SBC,
                            codec_extra: vec![0x11, 0x15, 2, 250],
                        }];
                        match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                            Ok(resp) => {
                                fx_log_info!("Reconfigure was successful {:?}", resp);
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                fx_log_info!("Stream {} reconfigure failed: {:?}", endpoint_id, e);
                                responder.send(&mut Err(PeerError::ProtocolError))?;
                            }
                        }
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} suspend failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Process all the requests from a peer controler.
    /// Finishes when an error occurs on the stream or the stream is closed (the controller
    /// disconnects).  Errors that happen within each specific request are logged but do not close
    /// the stream.
    async fn process_requests(
        mut stream: PeerControllerRequestStream,
        peer: Arc<Peer>,
        peer_id: PeerId,
    ) -> Result<(), failure::Error> {
        let mut streams = None;
        while let Some(req) = stream.try_next().await? {
            if streams.is_none() {
                // Discover the streams
                streams = Some(peer.discover().await?);
            }

            // Only need to handle requests for one stream
            let info = match streams.as_ref().expect("should be some").first() {
                Some(stream_info) => stream_info,
                None => {
                    // Try to discover again next time.
                    fx_log_info!("Can't execute {:?} - no streams exist on the peer.", req);
                    streams = None;
                    continue;
                }
            };

            fx_log_info!("handle_controller_request for id: {}, {:?}", peer_id, info);
            let fut = Self::handle_controller_request(peer.clone(), req, info.id());
            if let Err(e) = fut.await {
                fx_log_warn!("{} Error handling request: {:?}", peer_id, e);
            }
        }
        fx_log_info!("AvdtpController finished for id: {}", peer_id);
        Ok(())
    }
}

/// Control implementation to handle fidl requests.
/// State is stored in the remotes object.
async fn start_control_service(
    mut stream: PeerManagerRequestStream,
    peers: Arc<Mutex<HashMap<PeerId, Arc<Peer>>>>,
) -> Result<(), failure::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerRequest::GetPeer { peer_id, handle, .. } => {
                let handle_to_client: fidl::endpoints::ServerEnd<PeerControllerMarker> = handle;
                let peer_id: PeerId = peer_id.into();
                let peer = match peers.lock().get(&peer_id) {
                    None => {
                        fx_log_info!("GetPeer: request for nonexistent peer {}, closing.", peer_id);
                        // Dropping the handle will close the connection.
                        continue;
                    }
                    Some(peer) => peer.clone(),
                };
                fx_log_info!("GetPeer: Creating peer controller for peer with id {}.", peer_id);
                match handle_to_client.into_stream() {
                    Err(err) => {
                        fx_log_warn!(
                            "Error. Unable to create server endpoint from stream: {:?}.",
                            err
                        );
                    }
                    Ok(client_stream) => fasync::spawn(async move {
                        AvdtpController::process_requests(client_stream, peer, peer_id)
                            .await
                            .unwrap_or_else(|e| fx_log_err!("Requests failed: {:?}", e))
                    }),
                };
            }
            PeerManagerRequest::ConnectedPeers { responder } => {
                let mut connected_peers: Vec<fidl_fuchsia_bluetooth::PeerId> =
                    peers.lock().keys().cloned().map(Into::into).collect();
                responder.send(&mut connected_peers.iter_mut())?;
                fx_log_info!("ConnectedPeers request. Peers: {:?}", connected_peers);
            }
        }
    }
    Ok(())
}

/// A pool of peers which can be potentially controlled by a connected PeerManager protocol.
/// Each peer will persist in `peers` until both the peer is disconnected and a command fails.
pub struct AvdtpControllerPool {
    peers: Arc<Mutex<HashMap<PeerId, Arc<Peer>>>>,
    control_handle: Option<PeerManagerControlHandle>,
}

impl AvdtpControllerPool {
    pub fn new() -> Self {
        Self { control_handle: None, peers: Arc::new(Mutex::new(HashMap::new())) }
    }

    /// Called when a new client is connected to the PeerManager.  Accepts the stream of requests
    /// and spawns a new task for handling those requests.
    /// Only one PeerManager is allowed to be connected at once.  Later streams will be dropped.
    pub fn connected(&mut self, stream: PeerManagerRequestStream) {
        if self.control_handle.is_none() {
            self.control_handle = Some(stream.control_handle().clone());
            // Start parsing requests
            fasync::spawn(
                start_control_service(stream, self.peers.clone())
                    .unwrap_or_else(|e| fx_log_err!("Failed to spawn {:?}", e)),
            )
        }
    }

    /// When a peer is connected, notify the connected PeerManager that a peer has connected, and
    /// add it to the list of peers that requests get routed to.
    pub fn peer_connected(&mut self, peer_id: PeerId, peer: Arc<Peer>) {
        self.peers.lock().insert(peer_id, peer);
        if let Some(handle) = self.control_handle.as_ref() {
            if let Err(e) = handle.send_on_peer_connected(&mut peer_id.into()) {
                fx_log_info!("Peer connected callback failed: {:?}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{EndpointType, ErrorCode, Request, StreamInformation};
    use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_avdtp::*;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::{self, StreamExt};
    use std::convert::TryFrom;

    /// Reads from the AVDTP request stream, sending back acknowledgements, unless otherwise noted.
    async fn listen_for_avdtp_requests(remote: zx::Socket) {
        let remote_avdtp = Peer::new(remote).expect("remote control should be creatable");
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
    /// 1. Create the AvdtpControllerPool. This stores all active peers and handles listening
    /// to PeerManager and PeerController requests.
    /// 2. Send a `connected` signal to spawn the request listening task.
    /// 3. Send a `peer_connected` signal to test insertion of a remote Peer into the ControllerPool.
    /// 4. Mock a client sending a `get_peer()` FIDL command to test a client connecting to the ControllerPool.
    /// 5. Send client commands over the proxy, and make sure responses are valid and expected.
    /// Note: This test does not test the correctness of the underlying AVDTP commands. The AVDTP
    /// commands should be well tested in `bluetooth/lib/avdtp`.
    async fn test_client_connected_to_peer_manager() {
        // 1.
        let (pm_proxy, pm_stream) = create_proxy_and_stream::<PeerManagerMarker>().unwrap();
        let mut controller_pool = AvdtpControllerPool::new();

        // 2.
        controller_pool.connected(pm_stream);

        // 3.
        let fake_peer_id = PeerId(12345);
        let (remote, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let peer = Arc::new(Peer::new(signaling).expect("Should succeed"));
        controller_pool.peer_connected(fake_peer_id, peer);
        assert!(controller_pool.control_handle.is_some());
        assert!(controller_pool.peers.lock().contains_key(&fake_peer_id));

        // 4.
        let (client, server) =
            create_endpoints::<PeerControllerMarker>().expect("Couldn't create peer endpoint");
        let client_proxy = client.into_proxy().expect("Couldn't obtain client proxy");
        let res = pm_proxy.get_peer(&mut fake_peer_id.into(), server);
        assert_eq!(Ok(()), res.map_err(|e| e.to_string()));

        // 5.
        fasync::spawn(listen_for_avdtp_requests(remote));
        let res = client_proxy.set_configuration().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_configuration().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_capabilities().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.get_all_capabilities().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.suspend_stream().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.reconfigure_stream().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

        let res = client_proxy.suspend_and_reconfigure().await;
        assert_eq!(Ok(Ok(())), res.map_err(|e| e.to_string()));

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
