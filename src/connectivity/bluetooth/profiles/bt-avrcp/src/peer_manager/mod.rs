// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error as FailureError,
    bt_avctp::AvcPeer,
    fidl_fuchsia_bluetooth_avrcp::{
        AbsoluteVolumeHandlerProxy, AvcPanelCommand, TargetHandlerProxy,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::{self, fx_log_err, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::{mpsc, oneshot},
        stream::StreamExt,
    },
    parking_lot::{Mutex, RwLock},
    std::{collections::HashMap, sync::Arc},
};

mod target_delegate;

use crate::{
    peer::{Controller, RemotePeerHandle},
    profile::{AvrcpProfileEvent, AvrcpService, ProfileService},
    types::{PeerError as Error, PeerId},
};

pub use target_delegate::TargetDelegate;

#[derive(Debug)]
pub enum ServiceRequest {
    /// Request for a `Controller` given a `peer_id`.
    GetController { peer_id: PeerId, reply: oneshot::Sender<Controller> },

    /// Request to set the current volume handler. Returns an error if one is already set.
    RegisterAbsoluteVolumeHandler {
        absolute_volume_handler: AbsoluteVolumeHandlerProxy,
        reply: oneshot::Sender<Result<(), Error>>,
    },

    /// Request to set the current target handler. Returns an error if one is already set.
    RegisterTargetHandler {
        target_handler: TargetHandlerProxy,
        reply: oneshot::Sender<Result<(), Error>>,
    },
}

impl ServiceRequest {
    pub fn new_controller_request(
        peer_id: PeerId,
    ) -> (oneshot::Receiver<Controller>, ServiceRequest) {
        let (sender, receiver) = oneshot::channel();
        (receiver, ServiceRequest::GetController { peer_id: peer_id.clone(), reply: sender })
    }

    pub fn new_register_target_handler_request(
        target_handler: TargetHandlerProxy,
    ) -> (oneshot::Receiver<Result<(), Error>>, ServiceRequest) {
        let (sender, receiver) = oneshot::channel();
        (receiver, ServiceRequest::RegisterTargetHandler { target_handler, reply: sender })
    }

    pub fn new_register_absolute_volume_handler_request(
        absolute_volume_handler: AbsoluteVolumeHandlerProxy,
    ) -> (oneshot::Receiver<Result<(), Error>>, ServiceRequest) {
        let (sender, receiver) = oneshot::channel();
        (
            receiver,
            ServiceRequest::RegisterAbsoluteVolumeHandler {
                absolute_volume_handler,
                reply: sender,
            },
        )
    }
}

/// Creates, manages, and holds reference to all known peers by the AVRCP service. Handles incoming
/// service requests from FIDL and profile events from the BREDR service and dispatches them
/// accordingly.
pub struct PeerManager {
    profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,

    /// shared internal state storage for all connected peers.
    remotes: RwLock<HashMap<PeerId, RemotePeerHandle>>,

    /// Incoming requests by the service front end.
    service_request: mpsc::Receiver<ServiceRequest>,

    target_delegate: Arc<TargetDelegate>,
}

impl PeerManager {
    pub fn new(
        profile_svc: Box<dyn ProfileService + Send + Sync>,
        service_request: mpsc::Receiver<ServiceRequest>,
    ) -> Result<Self, FailureError> {
        Ok(Self {
            profile_svc: Arc::new(profile_svc),
            remotes: RwLock::new(HashMap::new()),
            service_request,
            target_delegate: Arc::new(TargetDelegate::new()),
        })
    }

    pub fn get_remote_peer(&self, peer_id: &str) -> RemotePeerHandle {
        self.remotes
            .write()
            .entry(peer_id.to_string())
            .or_insert_with(|| {
                RemotePeerHandle::spawn_peer(
                    peer_id.to_string(),
                    self.target_delegate.clone(),
                    self.profile_svc.clone(),
                )
            })
            .clone()
    }

    /// Handle a new incoming connection by a remote peer.
    fn new_control_connection(&self, peer_id: &PeerId, channel: zx::Socket) {
        let peer_handle = self.get_remote_peer(peer_id);
        match AvcPeer::new(channel) {
            Ok(peer) => {
                fx_vlog!(tag: "avrcp", 1, "new peer {:#?}", peer);
                peer_handle.set_control_connection(peer);
            }
            Err(e) => {
                fx_log_err!("Unable to make peer from socket {}: {:?}", peer_id, e);
            }
        }
    }

    fn handle_profile_service_event(&mut self, event: AvrcpProfileEvent) {
        match event {
            AvrcpProfileEvent::IncomingControlConnection { peer_id, channel } => {
                fx_vlog!(tag: "avrcp", 2, "IncomingConnection {} {:#?}", peer_id, channel);
                self.new_control_connection(&peer_id, channel);
            }
            AvrcpProfileEvent::ServicesDiscovered { peer_id, services } => {
                fx_vlog!(tag: "avrcp", 2, "ServicesDiscovered {} {:#?}", peer_id, services);
                let peer_handle = self.get_remote_peer(&peer_id);
                for service in services {
                    match service {
                        AvrcpService::Target { .. } => {
                            peer_handle.set_target_descriptor(service);
                        }
                        AvrcpService::Controller { .. } => {
                            peer_handle.set_controller_descriptor(service);
                        }
                    }
                }
            }
        }
    }

    pub fn handle_service_request(&mut self, service_request: ServiceRequest) {
        match service_request {
            ServiceRequest::GetController { peer_id, reply } => {
                // ignoring error if we failed to reply.
                let _ = reply.send(self.get_remote_peer(&peer_id).get_controller());
            }
            ServiceRequest::RegisterTargetHandler { target_handler, reply } => {
                let _ = reply.send(self.target_delegate.set_target_handler(target_handler));
            }
            ServiceRequest::RegisterAbsoluteVolumeHandler { absolute_volume_handler, reply } => {
                let _ = reply.send(
                    self.target_delegate.set_absolute_volume_handler(absolute_volume_handler),
                );
            }
        }
    }

    /// Loop to handle incoming events from the BREDR service and incoming service requests from FIDL.
    /// This will only return if there is an unrecoverable error.
    pub async fn run(mut self) -> Result<(), anyhow::Error> {
        let profile_service = self.profile_svc.clone();
        let mut profile_evt = profile_service.take_event_stream().fuse();
        loop {
            futures::select! {
                request = self.service_request.select_next_some() => {
                    self.handle_service_request(request);
                },
                evt = profile_evt.select_next_some() => {
                    self.handle_profile_service_event(evt.map_err(|e| {
                            fx_log_err!("profile service error {:?}", e);
                            FailureError::from(e)
                        })?
                    );
                },
            }
        }
    }
}
