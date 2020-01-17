// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error as FailureError},
    bt_avctp::{
        AvcCommand, AvcCommandResponse, AvcCommandType, AvcOpCode, AvcPacketType, AvcPeer,
        AvcResponseType, Error as AvctpError,
    },
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp::{
        AbsoluteVolumeHandlerProxy, AvcPanelCommand, MediaAttributes, PlayStatus,
        TargetHandlerProxy,
    },
    fidl_fuchsia_bluetooth_bredr::PSM_AVCTP,
    fuchsia_async as fasync,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::{mpsc, oneshot},
        future::FutureExt,
        ready,
        stream::{FusedStream, SelectAll, StreamExt, TryStreamExt},
        Stream,
    },
    parking_lot::{Mutex, RwLock},
    pin_utils::pin_mut,
    std::{
        collections::HashMap,
        convert::TryFrom,
        pin::Pin,
        string::String,
        sync::Arc,
        task::{Context, Poll},
    },
};

mod controller;
mod handlers;
mod notification_stream;
mod remote_peer;
mod target_delegate;

use crate::{
    packets::{Error as PacketError, *},
    profile::{AvrcpProfileEvent, AvrcpService, ProfileService},
    types::{PeerError as Error, PeerId},
};

pub use controller::{Controller, ControllerEvent, ControllerEventStream};
use handlers::ControlChannelHandler;
use notification_stream::NotificationStream;
use remote_peer::{RemotePeer, RemotePeerHandle};
use target_delegate::TargetDelegate;

#[derive(Debug)]
pub enum ServiceRequest {
    /// Request for a `Controller` given a `peer_id`.
    Controller { peer_id: PeerId, reply: oneshot::Sender<Controller> },

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
        (receiver, ServiceRequest::Controller { peer_id: peer_id.clone(), reply: sender })
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

pub struct PeerManager {
    profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,

    /// shared internal state storage for all connected peers.
    remotes: RwLock<HashMap<PeerId, Arc<RemotePeerHandle>>>,

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

    fn insert_peer_if_needed(&self, peer_id: &str) {
        let mut r = self.remotes.write();
        if !r.contains_key(peer_id) {
            let peer = RemotePeer::new(
                String::from(peer_id),
                self.target_delegate.clone(),
                self.profile_svc.clone(),
            );
            r.insert(String::from(peer_id), peer.clone());

            fasync::spawn(async move {
                RemotePeer::state_watcher(peer).await;
            })
        }
    }

    pub fn get_remote_peer(&self, peer_id: &str) -> Arc<RemotePeerHandle> {
        self.insert_peer_if_needed(peer_id);
        self.remotes.read().get(peer_id).unwrap().clone()
    }

    /// Handle a new incoming connection by a remote peer.
    /// TODO(2747): Properly handle the case where we are attempting to connect to remote at the
    ///       same time they connect to us according according to how the the spec says.
    ///       We need to keep a timestamp of when we opened an connection. If it's within a
    ///       specific window of us receiving an another connection we need to close both and
    ///       wait random interval, and attempt to reconnect. If it's outside that window, take the
    ///       last one.
    fn new_control_connection(&self, peer_id: &PeerId, channel: zx::Socket) {
        let remote_peer = self.get_remote_peer(peer_id);
        match AvcPeer::new(channel) {
            Ok(peer) => {
                fx_vlog!(tag: "avrcp", 1, "new peer {:#?}", peer);
                RemotePeer::set_control_connection(&remote_peer, peer);
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
                let remote_peer = self.get_remote_peer(&peer_id);
                for service in services {
                    match service {
                        AvrcpService::Target { .. } => {
                            RemotePeer::set_target_descriptor(&remote_peer, service);
                        }
                        AvrcpService::Controller { .. } => {
                            RemotePeer::set_controller_descriptor(&remote_peer, service);
                        }
                    }
                }
            }
        }
    }

    pub fn handle_service_request(&mut self, service_request: ServiceRequest) {
        match service_request {
            ServiceRequest::Controller { peer_id, reply } => {
                let peer = self.get_remote_peer(&peer_id);
                let peer_controller = Controller::new(peer);
                // ignoring error if we failed to reply.
                let _ = reply.send(peer_controller);
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

    /// Returns a future that will pump the event stream from the BREDR service, incoming peer
    /// requests from the FIDL frontend, and all the event streams for all control channels
    /// connected. The future returned by this function should only complete if there is an
    /// unrecoverable error in the peer manager.
    pub async fn run(&mut self) -> Result<(), anyhow::Error> {
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
