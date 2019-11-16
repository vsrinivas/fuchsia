// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::{
        AvcCommand, AvcCommandResponse, AvcCommandStream, AvcCommandType, AvcOpCode, AvcPacketType,
        AvcPeer, AvcResponseType, Error as AvctpError,
    },
    failure::{format_err, Error as FailureError},
    fidl::encoding::Decodable as FidlDecodable,
    fidl_fuchsia_bluetooth_avrcp::{AvcPanelCommand, MediaAttributes, PlayStatus},
    fidl_fuchsia_bluetooth_bredr::PSM_AVCTP,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::{mpsc, oneshot},
        future::{BoxFuture, FutureExt},
        ready,
        stream::{FusedStream, FuturesUnordered, SelectAll, StreamExt, TryStreamExt},
        Stream,
    },
    parking_lot::{Mutex, RwLock, RwLockUpgradableReadGuard},
    pin_utils::pin_mut,
    std::{
        collections::HashMap,
        convert::TryFrom,
        pin::Pin,
        string::String,
        sync::{Arc, Weak},
        task::{Context, Poll},
    },
};

mod controller;
mod handlers;
mod notification_stream;
mod remote_peer;

use crate::{
    packets::{Error as PacketError, *},
    profile::{AvrcpProfileEvent, AvrcpService, ProfileService},
    types::{PeerError as Error, PeerId, PeerIdStreamMap},
};

pub use controller::{Controller, ControllerEvent, ControllerEventStream};
use handlers::ControlChannelHandler;
use notification_stream::NotificationStream;
use remote_peer::{PeerChannel, RemotePeer};

#[derive(Debug)]
pub struct ControllerRequest {
    /// The peer id that the peer manager client wants obtain a controller for.
    peer_id: PeerId,
    /// The async reply back to the calling task with the PeerController.
    reply: oneshot::Sender<Controller>,
}

impl ControllerRequest {
    pub fn new(peer_id: PeerId) -> (oneshot::Receiver<Controller>, ControllerRequest) {
        let (sender, receiver) = oneshot::channel();
        (receiver, Self { peer_id: peer_id.clone(), reply: sender })
    }
}

pub struct PeerManager<'a> {
    /// shared internal state storage for all connected peers.
    inner: Arc<PeerManagerInner>,

    /// Incoming requests to obtain a PeerController proxy to a given peer id. Typically requested
    /// by the frontend FIDL service. Using a channel so we can serialize all peer connection related
    /// functions on to peer managers single select loop.
    peer_request: mpsc::Receiver<ControllerRequest>,

    /// Futures waiting on an outgoing connect attempt to remote peer's control channel PSM.
    new_control_connection_futures:
        FuturesUnordered<BoxFuture<'a, Result<(PeerId, zx::Socket), Error>>>,

    /// Collection of event streams of incoming command packets from all connected peer control
    /// channel connections.
    /// PeerIdStreamMap wraps all the contained AvcCommandStreams to provide an associated peer id
    /// with all commands received from the remote peer.
    control_channel_streams: SelectAll<PeerIdStreamMap<AvcCommandStream>>,

    /// Collection of futures that are pumping notifications on connected peers
    notification_futures: FuturesUnordered<BoxFuture<'a, ()>>,
}

impl<'a> PeerManager<'a> {
    pub fn new(
        profile_svc: Box<dyn ProfileService + Send + Sync>,
        peer_request: mpsc::Receiver<ControllerRequest>,
    ) -> Result<Self, FailureError> {
        Ok(Self {
            inner: Arc::new(PeerManagerInner::new(profile_svc)),
            peer_request,
            new_control_connection_futures: FuturesUnordered::new(),
            control_channel_streams: SelectAll::new(),
            notification_futures: FuturesUnordered::new(),
        })
    }

    fn connect_remote_control_psm(&mut self, peer_id: &str, psm: u16) {
        let remote_peer = self.inner.get_remote_peer(peer_id);

        let connection = remote_peer.control_channel.upgradable_read();
        match *connection {
            PeerChannel::Disconnected => {
                let mut conn = RwLockUpgradableReadGuard::upgrade(connection);
                *conn = PeerChannel::Connecting;
                let inner = self.inner.clone();
                let peer_id = String::from(peer_id);
                self.new_control_connection_futures.push(
                    async move {
                        let socket = inner
                            .profile_svc
                            .connect_to_device(&peer_id, psm)
                            .await
                            .map_err(|e| {
                                Error::ConnectionFailure(format_err!("Connection error: {:?}", e))
                            })?;
                        Ok((peer_id, socket))
                    }
                    .boxed(),
                );
            }
            _ => return,
        }
    }

    /// Handle a new incoming connection by a remote peer.
    /// TODO(2747): Properly handle the case where we are attempting to connect to remote at the
    ///       same time they connect to us according according to how the the spec says.
    ///       We need to keep a timestamp of when we opened an connection. If it's within a
    ///       specific window of us receiving an another connection we need to close both and
    ///       wait random interval, and attempt to reconnect. If it's outside that window, take the
    ///       last one.
    fn new_control_connection(&self, peer_id: &PeerId, channel: zx::Socket) {
        let remote_peer = self.inner.get_remote_peer(peer_id);
        match AvcPeer::new(channel) {
            Ok(peer) => {
                fx_vlog!(tag: "avrcp", 1, "new peer {:#?}", peer);
                let mut connection = remote_peer.control_channel.write();
                remote_peer.reset_command_handler();
                *connection = PeerChannel::Connected(Arc::new(peer));
            }
            Err(e) => {
                fx_log_err!("Unable to make peer from socket {}: {:?}", peer_id, e);
            }
        }
    }

    /// Called to test the state of peer discovery, connect to any L2CAP sockets if not already
    /// connected, and to setup and begin communication with the peer if possible. We need remote
    /// service profiles for our peer and established L2CAP connection before we can start
    /// communicating. This function will attempt to make an outgoing connection if we don't have
    /// one already. It's possible the remote has already connected to us before we get an service
    /// profile. It's also possible we receive an SDP profile before they connect.
    fn check_peer_state(&mut self, peer_id: &PeerId) {
        fx_vlog!(tag: "avrcp", 2, "check_peer_state {:#?}", peer_id);
        let remote_peer = self.inner.get_remote_peer(peer_id);

        let mut command_handle_guard = remote_peer.command_handler.lock();

        // Check if we are connected to the device and processing data
        if command_handle_guard.is_some() {
            fx_vlog!(tag: "avrcp", 2, "check_peer_state, already processing stream {:?}", peer_id);
            return;
        }

        // Have we received service profile data to know if the remote is a target or controller?
        if remote_peer.target_descriptor.read().is_none()
            && remote_peer.controller_descriptor.read().is_none()
        {
            // we don't have the profile information on this steam.
            fx_vlog!(tag: "avrcp", 2, "check_peer_state, no profile descriptor yet {:?}", peer_id);
            return;
        }

        let connection = remote_peer.control_channel.read();
        match connection.connection() {
            Some(peer_connection) => {
                fx_vlog!(tag: "avrcp", 2, "check_peer_state, setting up command handler and pumping notifications {:?}", peer_id);
                // we have a connection with a profile descriptor but we aren't processing it yet.

                *command_handle_guard =
                    Some(ControlChannelHandler::new(Arc::downgrade(&remote_peer)));

                let stream = PeerIdStreamMap::new(peer_connection.take_command_stream(), peer_id);
                self.control_channel_streams.push(stream);

                // TODO: if the remote is not acting as target according to SDP, do not pump notifications.
                self.notification_futures
                    .push(Self::pump_peer_notifications(self.inner.clone(), peer_id.clone()));
            }
            None => {
                // drop our write guard
                drop(connection);
                // we have a profile descriptor but we aren't connected to it yet.
                // TODO: extract out the dynamic PSM from the profile instead of using standard.
                self.connect_remote_control_psm(peer_id, PSM_AVCTP as u16);
            }
        }
    }

    fn handle_profile_service_event(&mut self, event: AvrcpProfileEvent) {
        match event {
            AvrcpProfileEvent::IncomingControlConnection { peer_id, channel } => {
                fx_vlog!(tag: "avrcp", 2, "IncomingConnection {} {:#?}", peer_id, channel);
                self.new_control_connection(&peer_id, channel);
                self.check_peer_state(&peer_id);
            }
            AvrcpProfileEvent::ServicesDiscovered { peer_id, services } => {
                fx_vlog!(tag: "avrcp", 2, "ServicesDiscovered {} {:#?}", peer_id, services);
                let remote_peer = self.inner.get_remote_peer(&peer_id);
                for service in services {
                    match service {
                        AvrcpService::Target { .. } => {
                            let mut profile_descriptor = remote_peer.target_descriptor.write();
                            *profile_descriptor = Some(service);
                        }
                        AvrcpService::Controller { .. } => {
                            let mut profile_descriptor = remote_peer.controller_descriptor.write();
                            *profile_descriptor = Some(service);
                        }
                    }
                }

                self.check_peer_state(&peer_id);
            }
        }
    }

    fn pump_peer_notifications(inner: Arc<PeerManagerInner>, peer_id: PeerId) -> BoxFuture<'a, ()> {
        async move {
            // events we support when speaking to a peer that supports the target profile.
            const SUPPORTED_NOTIFICATIONS: [NotificationEventId; 4] = [
                NotificationEventId::EventPlaybackStatusChanged,
                NotificationEventId::EventTrackChanged,
                NotificationEventId::EventPlaybackPosChanged,
                NotificationEventId::EventVolumeChanged,
            ];

            let supported_notifications: Vec<NotificationEventId> =
                SUPPORTED_NOTIFICATIONS.iter().cloned().collect();

            let peer = inner.get_remote_peer(&peer_id);

            // look up what notifications we support on this peer first
            let remote_supported_notifications = match peer.get_supported_events().await {
                Ok(x) => x,
                Err(_) => return,
            };

            let supported_notifications: Vec<NotificationEventId> =
                remote_supported_notifications
                    .into_iter()
                    .filter(|k| supported_notifications.contains(k))
                    .collect();

            // TODO(36320): move to remote peer.
            fn handle_response(
                notif: &NotificationEventId,
                peer: &Arc<RemotePeer>,
                data: &[u8],
            ) -> Result<bool, Error> {
                fx_vlog!(tag: "avrcp", 2, "received notification for {:?} {:?}", notif, data);

                let preamble =
                    VendorDependentPreamble::decode(data).map_err(|e| Error::PacketError(e))?;

                let data = &data[preamble.encoded_len()..];

                if data.len() < preamble.parameter_length as usize {
                    return Err(Error::UnexpectedResponse);
                }

                match notif {
                    NotificationEventId::EventPlaybackStatusChanged => {
                        let response = PlaybackStatusChangedNotificationResponse::decode(data)
                            .map_err(|e| Error::PacketError(e))?;
                        peer.broadcast_event(ControllerEvent::PlaybackStatusChanged(
                            response.playback_status(),
                        ));
                        Ok(false)
                    }
                    NotificationEventId::EventTrackChanged => {
                        let response = TrackChangedNotificationResponse::decode(data)
                            .map_err(|e| Error::PacketError(e))?;
                        peer.broadcast_event(ControllerEvent::TrackIdChanged(
                            response.identifier(),
                        ));
                        Ok(false)
                    }
                    NotificationEventId::EventPlaybackPosChanged => {
                        let response = PlaybackPosChangedNotificationResponse::decode(data)
                            .map_err(|e| Error::PacketError(e))?;
                        peer.broadcast_event(ControllerEvent::PlaybackPosChanged(
                            response.position(),
                        ));
                        Ok(false)
                    }
                    _ => Ok(true),
                }
            }

            let mut notification_streams = SelectAll::new();

            for notif in supported_notifications {
                fx_vlog!(tag: "avrcp", 2, "creating notification stream for {:#?}", notif);
                let stream = NotificationStream::new(peer.clone(), notif, 5).map_ok(move |data| (notif, data) );
                notification_streams.push(stream);
            }

            pin_mut!(notification_streams);
            loop {
                if futures::select! {
                    event_result = notification_streams.select_next_some() => {
                        match event_result {
                            Ok((notif, data)) => {
                                handle_response(&notif, &peer, &data[..])
                                    .unwrap_or_else(|e| { fx_log_err!("Error decoding packet from peer {:?}", e); true} )
                            },
                            Err(Error::CommandNotSupported) => false,
                            Err(_) => true,
                            _=> true,
                        }
                    }
                    complete => { true }
                }
                {
                    break;
                }
            }
            fx_vlog!(tag: "avrcp", 2, "stopping notifications for {:#?}", peer_id);
        }
            .boxed()
    }

    /// Returns a future that will pump the event stream from the BREDR service, incoming peer
    /// requests from the FIDL frontend, and all the event streams for all control channels
    /// connected. The future returned by this function should only complete if there is an
    /// unrecoverable error in the peer manager.
    /// TODO(36320): refactor remote peer related work into RemotePeer and out of PeerManager.
    fn handle_control_channel_command(
        &self,
        peer_id: &PeerId,
        command: Result<AvcCommand, bt_avctp::Error>,
    ) {
        let remote_peer = self.inner.get_remote_peer(peer_id);
        let mut close_connection = false;

        match command {
            Ok(avcommand) => {
                {
                    // scoped MutexGuard
                    let cmd_handler_lock = remote_peer.command_handler.lock();
                    match cmd_handler_lock.as_ref() {
                        Some(cmd_handler) => {
                            if let Err(e) =
                                cmd_handler.handle_command(avcommand, self.inner.clone())
                            {
                                fx_log_err!("control channel command handler error {:?}", e);
                                close_connection = true;
                            }
                        }
                        None => {
                            debug_assert!(false, "pumping control channel without cmd_handler");
                        }
                    }
                }
            }
            Err(avctp_error) => {
                fx_log_err!("received error from control channel {:?}", avctp_error);
                close_connection = true;
            }
        }
        if close_connection {
            remote_peer.reset_connection();
        }
    }

    /// Returns a future that will pump the event stream from the BREDR service, incoming peer
    /// requests from the FIDL frontend, and all the event streams for all control channels
    /// connected. The future returned by this function should only complete if there is an
    /// unrecoverable error in the peer manager.
    pub async fn run(&mut self) -> Result<(), failure::Error> {
        let inner = self.inner.clone();
        let mut profile_evt = inner.profile_svc.take_event_stream().fuse();
        loop {
            futures::select! {
                (peer_id, command) = self.control_channel_streams.select_next_some() => {
                    self.handle_control_channel_command(&peer_id, command);
                },
                request = self.peer_request.select_next_some() => {
                    let peer = inner.get_remote_peer(&request.peer_id);
                    let peer_controller = Controller::new(peer);
                    // ignoring error if we failed to reply.
                    let _ = request.reply.send(peer_controller);
                },
                evt = profile_evt.select_next_some() => {
                    self.handle_profile_service_event(evt.map_err(|e| {
                            fx_log_err!("profile service error {:?}", e);
                            FailureError::from(e)
                        })?
                    );
                },
                result = self.new_control_connection_futures.select_next_some() => {
                    match result {
                        Ok((peer_id, socket)) => {
                            self.new_control_connection(&peer_id, socket);
                            self.check_peer_state(&peer_id);
                        }
                        Err(e) => {
                            fx_log_err!("connection error {:?}", e);
                        }
                    }
                },
                _= self.notification_futures.select_next_some() => {}
            }
        }
    }
}

#[derive(Debug)]
pub struct PeerManagerInner {
    profile_svc: Box<dyn ProfileService + Send + Sync>,
    remotes: RwLock<HashMap<PeerId, Arc<RemotePeer>>>,
    // TODO(1279): implement the target handler.
}

impl PeerManagerInner {
    fn new(profile_svc: Box<dyn ProfileService + Send + Sync>) -> Self {
        Self { profile_svc, remotes: RwLock::new(HashMap::new()) }
    }

    fn insert_if_needed(&self, peer_id: &str) {
        let mut r = self.remotes.write();
        if !r.contains_key(peer_id) {
            r.insert(String::from(peer_id), Arc::new(RemotePeer::new(String::from(peer_id))));
        }
    }

    pub fn get_remote_peer(&self, peer_id: &str) -> Arc<RemotePeer> {
        self.insert_if_needed(peer_id);
        self.remotes.read().get(peer_id).unwrap().clone()
    }
}
