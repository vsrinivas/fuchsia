// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp as avdtp,
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

/// A structure that handles a requests from peer controller, specific to a peer.
struct AvdtpController {}

impl AvdtpController {
    /// Handle one request from the PeerController and respond with the results from sending the
    /// comamnd(s) to the peer
    async fn handle_controller_request(
        avdtp: Arc<avdtp::Peer>,
        request: PeerControllerRequest,
        endpoint_id: &avdtp::StreamEndpointId,
    ) -> Result<(), fidl::Error> {
        match request {
            PeerControllerRequest::SetConfiguration { responder } => {
                let generic_capabilities = vec![avdtp::ServiceCapability::MediaTransport];
                match avdtp.set_configuration(endpoint_id, endpoint_id, &generic_capabilities).await
                {
                    Ok(resp) => fx_log_info!("SetConfiguration successful: {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} set_configuration failed: {:?}", endpoint_id, e);
                        match e {
                            avdtp::Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
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
                    Ok(resp) => fx_log_info!("SuspendStream was successful {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} suspend failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::ReconfigureStream { responder } => {
                // Only one frequency, channel mode, block length, subband,
                // and allocation for reconfigure (A2DP 4.3.2)
                let generic_capabilities = vec![avdtp::ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                    codec_extra: vec![0x11, 0x15, 2, 250],
                }];
                match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                    Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} reconfigure failed: {:?}", endpoint_id, e);
                        match e {
                            avdtp::Error::RemoteConfigRejected(_, _) => {}
                            _ => responder.send(&mut Err(PeerError::ProtocolError))?,
                        }
                    }
                }
            }
            PeerControllerRequest::ReleaseStream { responder } => {
                match avdtp.close(endpoint_id).await {
                    Ok(resp) => fx_log_info!("ReleaseStream was successful: {:?}", resp),
                    Err(e) => {
                        fx_log_info!("Stream {} release failed: {:?}", endpoint_id, e);
                        responder.send(&mut Err(PeerError::ProtocolError))?;
                    }
                }
            }
            PeerControllerRequest::EstablishStream { responder } => {
                match avdtp.open(endpoint_id).await {
                    Ok(resp) => fx_log_info!("EstablishStream was successful: {:?}", resp),
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
                        let generic_capabilities = vec![avdtp::ServiceCapability::MediaCodec {
                            media_type: avdtp::MediaType::Audio,
                            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
                            codec_extra: vec![0x11, 0x15, 2, 250],
                        }];

                        match avdtp.reconfigure(endpoint_id, &generic_capabilities[..]).await {
                            Ok(resp) => fx_log_info!("Reconfigure was successful {:?}", resp),
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
        peer: Arc<avdtp::Peer>,
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
    peers: Arc<Mutex<HashMap<PeerId, Arc<avdtp::Peer>>>>,
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
                    Err(err) => fx_log_warn!(
                        "Error. Unable to create server endpoint from stream: {:?}.",
                        err
                    ),
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
    peers: Arc<Mutex<HashMap<PeerId, Arc<avdtp::Peer>>>>,
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
    pub fn peer_connected(&mut self, peer_id: PeerId, peer: Arc<avdtp::Peer>) {
        self.peers.lock().insert(peer_id, peer);
        if let Some(handle) = self.control_handle.as_ref() {
            if let Err(e) = handle.send_on_peer_connected(&mut peer_id.into()) {
                fx_log_info!("Peer connected callback failed: {:?}", e);
            }
        }
    }
}
