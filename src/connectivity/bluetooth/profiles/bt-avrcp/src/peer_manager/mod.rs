// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error as FailureError,
    bt_avctp::{AvcPeer, AvctpPeer},
    fidl_fuchsia_bluetooth_avrcp::{
        AbsoluteVolumeHandlerProxy, AvcPanelCommand, TargetHandlerProxy,
    },
    fidl_fuchsia_bluetooth_bredr::ProfileProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    fuchsia_bluetooth::types::PeerId,
    futures::{self, channel::oneshot, stream::StreamExt},
    log::trace,
    parking_lot::{Mutex, RwLock},
    std::{collections::HashMap, sync::Arc},
};

mod target_delegate;

use crate::{
    peer::{Controller, RemotePeerHandle},
    profile::AvrcpService,
    types::PeerError as Error,
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
    profile_proxy: ProfileProxy,

    /// Connected peers, which may be connected or disconnected
    remotes: RwLock<HashMap<PeerId, RemotePeerHandle>>,

    target_delegate: Arc<TargetDelegate>,
}

impl PeerManager {
    pub fn new(profile_proxy: ProfileProxy) -> Result<Self, FailureError> {
        Ok(Self {
            profile_proxy,
            remotes: RwLock::new(HashMap::new()),
            target_delegate: Arc::new(TargetDelegate::new()),
        })
    }

    pub fn get_remote_peer(&self, peer_id: &PeerId) -> RemotePeerHandle {
        self.remotes
            .write()
            .entry(peer_id.clone())
            .or_insert_with(|| {
                RemotePeerHandle::spawn_peer(
                    peer_id.clone(),
                    self.target_delegate.clone(),
                    self.profile_proxy.clone(),
                )
            })
            .clone()
    }

    /// Handle a new incoming connection by a remote peer.
    pub fn new_control_connection(&self, peer_id: &PeerId, channel: Channel) {
        let peer_handle = self.get_remote_peer(peer_id);
        let peer = AvcPeer::new(channel);
        peer_handle.set_control_connection(peer);
    }

    /// Handle a new incoming browse channel connection by a remote peer.
    pub fn new_browse_connection(&mut self, peer_id: &PeerId, channel: Channel) {
        let peer_handle = self.get_remote_peer(peer_id);
        let peer = AvctpPeer::new(channel);
        trace!("new browse peer {:#?}", peer);
        peer_handle.set_browse_connection(peer);
    }

    pub fn services_found(&mut self, peer_id: &PeerId, services: Vec<AvrcpService>) {
        trace!("ServicesDiscovered {} {:#?}", peer_id, services);
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
}
