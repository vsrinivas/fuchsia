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
    fidl_fuchsia_bluetooth_avrcp::{AvcPanelCommand, MediaAttributes},
    fidl_fuchsia_bluetooth_bredr::PSM_AVCTP,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        self, channel::mpsc, channel::oneshot, future::BoxFuture, future::FutureExt, ready,
        stream::FusedStream, stream::FuturesUnordered, stream::SelectAll, stream::StreamExt,
        stream::TryStreamExt, task::Context, Poll, Stream,
    },
    parking_lot::{Mutex, RwLock, RwLockUpgradableReadGuard},
    pin_utils::pin_mut,
    std::{
        collections::HashMap, convert::TryFrom, pin::Pin, string::String, sync::Arc, sync::Weak,
    },
};

use crate::{
    packets::{Error as PacketError, *},
    profile::{AvrcpProfileEvent, AvrcpService, ProfileService},
    types::{PeerError as Error, PeerId, PeerIdStreamMap},
};

#[derive(Debug, Clone)]
pub enum PeerControllerEvent {
    PlaybackStatusChanged(PlaybackStatus),
    TrackIdChanged(u64),
    PlaybackPosChanged(u32),
}

pub type PeerControllerEventStream = mpsc::Receiver<PeerControllerEvent>;

#[derive(Debug)]
pub struct PeerControllerRequest {
    /// The peer id that the peer manager client wants obtain a peer controller for.
    peer_id: PeerId,
    /// The async reply back to the calling task with the PeerController.
    reply: oneshot::Sender<PeerController>,
}

impl PeerControllerRequest {
    pub fn new(peer_id: PeerId) -> (oneshot::Receiver<PeerController>, PeerControllerRequest) {
        let (sender, receiver) = oneshot::channel();
        (receiver, Self { peer_id: peer_id.clone(), reply: sender })
    }
}

type RemotePeersMap = HashMap<PeerId, Arc<RemotePeer>>;

pub struct PeerManager<'a> {
    /// shared internal state storage for all connected peers.
    inner: Arc<PeerManagerInner>,

    /// Incoming requests to obtain a PeerController proxy to a given peer id. Typically requested
    /// by the frontend FIDL service. Using a channel so we can serialize all peer connection related
    /// functions on to peer managers single select loop.
    peer_request: mpsc::Receiver<PeerControllerRequest>,

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
        peer_request: mpsc::Receiver<PeerControllerRequest>,
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

        // are we already connected to the device and processing data?
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
                    Some(ControlChannelCommandHandler::new(Arc::downgrade(&remote_peer)));

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

            // look up what notifications we support on this peer first
            let remote_supported_notifications = match inner.get_supported_events(&peer_id).await {
                Ok(x) => x,
                Err(_) => return,
            };

            let supported_notifications: Vec<NotificationEventId> =
                remote_supported_notifications
                    .into_iter()
                    .filter(|k| supported_notifications.contains(k))
                    .collect();

            let peer = inner.get_remote_peer(&peer_id);

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
                        peer.broadcast_event(PeerControllerEvent::PlaybackStatusChanged(
                            response.playback_status(),
                        ));
                        Ok(false)
                    }
                    NotificationEventId::EventTrackChanged => {
                        let response = TrackChangedNotificationResponse::decode(data)
                            .map_err(|e| Error::PacketError(e))?;
                        peer.broadcast_event(PeerControllerEvent::TrackIdChanged(
                            response.identifier(),
                        ));
                        Ok(false)
                    }
                    NotificationEventId::EventPlaybackPosChanged => {
                        let response = PlaybackPosChangedNotificationResponse::decode(data)
                            .map_err(|e| Error::PacketError(e))?;
                        peer.broadcast_event(PeerControllerEvent::PlaybackPosChanged(
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

    /// Handles an incoming AVC command received by one of the peer's control channels and
    /// dispatches it to the appropriate command handler registered with that peer.
    /// If an error occurs processing a command, we close the control handle.
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
    /// TODO(36320): refactor remote peer related work into RemotePeer and out of PeerManager.
    pub async fn run(&mut self) -> Result<(), failure::Error> {
        let inner = self.inner.clone();
        let mut profile_evt = inner.profile_svc.take_event_stream().fuse();
        loop {
            futures::select! {
                (peer_id, command) = self.control_channel_streams.select_next_some() => {
                    self.handle_control_channel_command(&peer_id, command);
                },
                request = self.peer_request.select_next_some() => {
                    let peer_controller =
                        PeerController { peer_id: request.peer_id.clone(), inner: self.inner.clone() };
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

#[derive(Debug, PartialEq)]
enum PeerChannel<T> {
    Connected(Arc<T>),
    Connecting,
    Disconnected,
}

impl<T> PeerChannel<T> {
    fn connection(&self) -> Option<Arc<T>> {
        match self {
            PeerChannel::Connected(t) => Some(t.clone()),
            _ => None,
        }
    }
}

/// Internal object to manage a remote peer
#[derive(Debug)]
struct RemotePeer {
    peer_id: PeerId,

    /// Contains the remote peer's target profile.
    target_descriptor: RwLock<Option<AvrcpService>>,

    /// Contains the remote peer's controller profile.
    controller_descriptor: RwLock<Option<AvrcpService>>,

    /// Control channel to the remote device.
    control_channel: RwLock<PeerChannel<AvcPeer>>,

    // TODO(BT-2221): add browse channel.
    // browse_channel: RwLock<PeerChannel<AvtcpPeer>>,
    //
    /// Contains a vec of all PeerControllers that have taken an event stream waiting for events from this peer.
    controller_listeners: Mutex<Vec<mpsc::Sender<PeerControllerEvent>>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel. Only set once we have enough information to determine
    /// our role based on the peer's SDP record.
    command_handler: Mutex<Option<ControlChannelCommandHandler>>,
}

impl RemotePeer {
    fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            control_channel: RwLock::new(PeerChannel::Disconnected),
            // TODO(BT-2221): add browse channel.
            //browse_channel: RwLock::new(PeerChannel::Disconnected),
            controller_listeners: Mutex::new(Vec::new()),
            target_descriptor: RwLock::new(None),
            controller_descriptor: RwLock::new(None),
            command_handler: Mutex::new(None),
        }
    }

    /// Enumerates all listening controller_listeners queues and sends a clone of the event to each
    fn broadcast_event(&self, event: PeerControllerEvent) {
        let mut listeners = self.controller_listeners.lock();
        // remove all the dead listeners from the list.
        listeners.retain(|i| !i.is_closed());
        for sender in listeners.iter_mut() {
            if let Err(send_error) = sender.try_send(event.clone()) {
                fx_log_err!(
                    "unable to send event to peer controller stream for {} {:?}",
                    self.peer_id,
                    send_error
                );
            }
        }
    }

    // Hold the write lock on control_channel before calling this.
    fn reset_command_handler(&self) {
        let mut cmd_handler = self.command_handler.lock();
        *cmd_handler = None;
    }

    fn reset_connection(&self) {
        let mut control_channel = self.control_channel.write();
        self.reset_command_handler();
        *control_channel = PeerChannel::Disconnected;
    }
}

/// Controller interface for a remote peer returned by the PeerManager using the
/// PeerControllerRequest stream for a given PeerControllerRequest.
#[derive(Debug)]
pub struct PeerController {
    // PeerController owns a reference to the PeerManagerInner directly.
    // Consider serializing peer controller ops over a channel to the peer manager loop
    // internally instead of directly using the inner. It's possible that the user of this object
    // will be executing on a different async executor than the PeerManager and will be competing
    // for the same locks that the peer manager which is trying to use which is not the most ideal
    // design potentially. Channelizing ops would guarantee that ops on the PeerManagerInner will
    // only happen on the PeerManager select loop only and we can possibly remove some of the locks
    // we have currently have inside the PeerManagerInner to deal with it being shared.
    inner: Arc<PeerManagerInner>,
    peer_id: PeerId,
}

impl PeerController {
    pub async fn send_avc_passthrough_keypress(&self, avc_keycode: u8) -> Result<(), Error> {
        self.inner.send_avc_passthrough_keypress(&self.peer_id, avc_keycode).await
    }

    pub async fn set_absolute_volume(&self, requested_volume: u8) -> Result<u8, Error> {
        self.inner.set_absolute_volume(&self.peer_id, requested_volume).await
    }

    pub async fn get_media_attributes(&self) -> Result<MediaAttributes, Error> {
        self.inner.get_media_attributes(&self.peer_id).await
    }

    pub async fn get_supported_events(&self) -> Result<Vec<NotificationEventId>, Error> {
        self.inner.get_supported_events(&self.peer_id).await
    }

    pub async fn send_raw_vendor_command<'a>(
        &'a self,
        pdu_id: u8,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, Error> {
        let command = RawVendorDependentPacket::new(PduId::try_from(pdu_id)?, payload);
        let remote = self.inner.get_remote_peer(&self.peer_id);
        let connection = remote.control_channel.read().connection();
        match connection {
            Some(peer) => {
                PeerManagerInner::send_status_vendor_dependent_command(&peer, &command).await
            }
            _ => Err(Error::RemoteNotFound),
        }
    }

    /// Informational only. Intended for logging only. Inherently racey.
    pub fn is_connected(&self) -> bool {
        let remote = self.inner.get_remote_peer(&self.peer_id);
        let connection = remote.control_channel.read();
        match *connection {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    pub fn take_event_stream(&self) -> PeerControllerEventStream {
        let (sender, receiver) = mpsc::channel(512);
        let remote = self.inner.get_remote_peer(&self.peer_id);
        remote.controller_listeners.lock().push(sender);
        receiver
    }
}

#[derive(Debug)]
struct PeerManagerInner {
    profile_svc: Box<dyn ProfileService + Send + Sync>,
    remotes: RwLock<RemotePeersMap>,
    // TODO(BT-686): implement the target handler.
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

    fn get_remote_peer(&self, peer_id: &str) -> Arc<RemotePeer> {
        self.insert_if_needed(peer_id);
        self.remotes.read().get(peer_id).unwrap().clone()
    }

    /// Send a generic "status" vendor dependent command and returns the result as a future.
    /// This method encodes the `command` packet, awaits and decodes all responses, will issue
    /// continuation commands for incomplete responses (eg "get_element_attributes" command), and
    /// will return a result of the decoded packet or an error for any non stable response received
    async fn send_status_vendor_dependent_command<'a>(
        peer: &'a AvcPeer,
        command: &'a impl VendorDependent,
    ) -> Result<Vec<u8>, Error> {
        let mut buf = vec![];
        let packet = command.encode_packet().expect("unable to encode packet");
        let mut stream = peer.send_vendor_dependent_command(AvcCommandType::Status, &packet[..])?;

        loop {
            let response = loop {
                if let Some(result) = stream.next().await {
                    let response: AvcCommandResponse = result.map_err(|e| Error::AvctpError(e))?;
                    fx_vlog!(tag: "avrcp", 1, "vendor response {:#?}", response);
                    match response.response_type() {
                        AvcResponseType::Interim => continue,
                        AvcResponseType::NotImplemented => return Err(Error::CommandNotSupported),
                        AvcResponseType::Rejected => return Err(Error::CommandFailed),
                        AvcResponseType::InTransition => return Err(Error::UnexpectedResponse),
                        AvcResponseType::Changed => return Err(Error::UnexpectedResponse),
                        AvcResponseType::Accepted => return Err(Error::UnexpectedResponse),
                        AvcResponseType::ImplementedStable => break response.1,
                    }
                } else {
                    return Err(Error::CommandFailed);
                }
            };

            match VendorDependentPreamble::decode(&response[..]) {
                Ok(preamble) => {
                    buf.extend_from_slice(&response[preamble.encoded_len()..]);
                    match preamble.packet_type() {
                        PacketType::Single | PacketType::Stop => {
                            break;
                        }
                        // Still more to decode. Queue up a continuation call.
                        _ => {}
                    }
                }
                Err(e) => {
                    fx_log_info!("Unable to parse vendor dependent preamble: {:?}", e);
                    return Err(Error::PacketError(e));
                }
            };

            let packet = RequestContinuingResponseCommand::new(u8::from(&command.pdu_id()))
                .encode_packet()
                .expect("unable to encode packet");

            stream = peer.send_vendor_dependent_command(AvcCommandType::Control, &packet[..])?;
        }
        Ok(buf)
    }

    async fn send_avc_passthrough_keypress<'a>(
        &'a self,
        peer_id: &'a str,
        avc_keycode: u8,
    ) -> Result<(), Error> {
        let remote = self.get_remote_peer(peer_id);
        {
            // key_press
            let payload_1 = &[avc_keycode, 0x00];
            let r = remote.control_channel.read().connection();
            if let Some(peer) = r {
                let response = peer.send_avc_passthrough_command(payload_1).await;
                match response {
                    Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {}
                    Ok(AvcCommandResponse(AvcResponseType::NotImplemented, _)) => {
                        return Err(Error::CommandNotSupported);
                    }
                    Err(e) => {
                        fx_log_err!("error sending avc command to {}: {:?}", peer_id, e);
                        return Err(Error::CommandFailed);
                    }
                    Ok(response) => {
                        fx_log_err!(
                            "error sending avc command. unhandled response {}: {:?}",
                            peer_id,
                            response
                        );
                        return Err(Error::CommandFailed);
                    }
                }
            } else {
                return Err(Error::RemoteNotFound);
            }
        }
        {
            // key_release
            let payload_2 = &[avc_keycode | 0x80, 0x00];
            let r = remote.control_channel.read().connection();
            if let Some(peer) = r {
                let response = peer.send_avc_passthrough_command(payload_2).await;
                match response {
                    Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {
                        return Ok(());
                    }
                    Ok(AvcCommandResponse(AvcResponseType::Rejected, _)) => {
                        fx_log_info!("avrcp command rejected {}: {:?}", peer_id, response);
                        return Err(Error::CommandNotSupported);
                    }
                    Err(e) => {
                        fx_log_err!("error sending avc command to {}: {:?}", peer_id, e);
                        return Err(Error::CommandFailed);
                    }
                    _ => {
                        fx_log_err!(
                            "error sending avc command. unhandled response {}: {:?}",
                            peer_id,
                            response
                        );
                        return Err(Error::CommandFailed);
                    }
                }
            } else {
                Err(Error::RemoteNotFound)
            }
        }
    }

    async fn set_absolute_volume<'a>(
        &'a self,
        peer_id: &'a PeerId,
        volume: u8,
    ) -> Result<u8, Error> {
        let remote = self.get_remote_peer(peer_id);
        let conn = remote.control_channel.read().connection();
        match conn {
            Some(peer) => {
                let cmd =
                    SetAbsoluteVolumeCommand::new(volume).map_err(|e| Error::PacketError(e))?;
                fx_vlog!(tag: "avrcp", 1, "set_absolute_volume send command {:#?}", cmd);
                let buf = Self::send_status_vendor_dependent_command(&peer, &cmd).await?;
                let response = SetAbsoluteVolumeResponse::decode(&buf[..])
                    .map_err(|e| Error::PacketError(e))?;
                fx_vlog!(tag: "avrcp", 1, "set_absolute_volume received response {:#?}", response);
                Ok(response.volume())
            }
            _ => Err(Error::RemoteNotFound),
        }
    }

    async fn get_media_attributes<'a>(
        &'a self,
        peer_id: &'a PeerId,
    ) -> Result<MediaAttributes, Error> {
        let remote = self.get_remote_peer(peer_id);
        let conn = remote.control_channel.read().connection();
        match conn {
            Some(peer) => {
                let mut media_attributes = MediaAttributes::new_empty();
                let cmd = GetElementAttributesCommand::all_attributes();
                fx_vlog!(tag: "avrcp", 1, "get_media_attributes send command {:#?}", cmd);
                let buf = Self::send_status_vendor_dependent_command(&peer, &cmd).await?;
                let response = GetElementAttributesResponse::decode(&buf[..])
                    .map_err(|e| Error::PacketError(e))?;
                fx_vlog!(tag: "avrcp", 1, "get_media_attributes received response {:#?}", response);
                media_attributes.title = response.title.unwrap_or("".to_string());
                media_attributes.artist_name = response.artist_name.unwrap_or("".to_string());
                media_attributes.album_name = response.album_name.unwrap_or("".to_string());
                media_attributes.track_number = response.track_number.unwrap_or("".to_string());
                media_attributes.total_number_of_tracks =
                    response.total_number_of_tracks.unwrap_or("".to_string());
                media_attributes.genre = response.genre.unwrap_or("".to_string());
                media_attributes.playing_time = response.playing_time.unwrap_or("".to_string());
                Ok(media_attributes)
            }
            _ => Err(Error::RemoteNotFound),
        }
    }

    async fn get_supported_events<'a>(
        &'a self,
        peer_id: &'a PeerId,
    ) -> Result<Vec<NotificationEventId>, Error> {
        let remote = self.get_remote_peer(peer_id);
        let conn = remote.control_channel.read().connection();
        match conn {
            Some(peer) => {
                let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
                fx_vlog!(tag: "avrcp", 1, "get_capabilities(events) send command {:#?}", cmd);
                let buf = Self::send_status_vendor_dependent_command(&peer, &cmd).await?;
                let capabilities =
                    GetCapabilitiesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
                let mut event_ids = vec![];
                for event_id in capabilities.event_ids() {
                    event_ids.push(NotificationEventId::try_from(event_id)?);
                }
                Ok(event_ids)
            }
            _ => Err(Error::RemoteNotFound),
        }
    }
}

/// Handles commands received from the peer, typically when we are acting in target role for A2DP
/// source and absolute volume support for A2DP sink. Maintains state such as continuations and
/// registered notifications by the peer.
/// FIXME: This is a stub that mostly logs incoming commands as we implement out features in TARGET.
#[derive(Debug)]
struct ControlChannelCommandHandler {
    /// Handle back to the remote peer. Weak to prevent a reference cycle since the remote peer owns this object.
    remote_peer: Weak<RemotePeer>,
    // TODO: implement continuations
    // Remaining packets packets as part of fragmented response.
    // Map of PduIDs to a Vec of remaining encoded packets.
    //#[allow(dead_code)]
    //continuations: HashMap<u8, Vec<Vec<u8>>>,
}

impl ControlChannelCommandHandler {
    fn new(remote_peer: Weak<RemotePeer>) -> Self {
        Self { remote_peer }
    }

    fn handle_passthrough_command(
        &self,
        remote_peer: &Arc<RemotePeer>,
        command: &AvcCommand,
    ) -> Result<AvcResponseType, Error> {
        let body = command.body();
        let command = AvcPanelCommand::from_primitive(body[0]);

        fx_log_info!("Received passthrough command {:x?} {}", command, &remote_peer.peer_id);

        match command {
            Some(_) => Ok(AvcResponseType::Accepted),
            None => Ok(AvcResponseType::Rejected),
        }
    }

    fn handle_notify_vendor_command(&self, _inner: &Arc<PeerManagerInner>, command: &AvcCommand) {
        let packet_body = command.body();

        let preamble = match VendorDependentPreamble::decode(packet_body) {
            Err(e) => {
                fx_log_err!("Unable to parse vendor dependent preamble: {:?}", e);
                // TODO: validate error response is correct. Consider dropping the connection?
                let _ = command.send_response(AvcResponseType::NotImplemented, packet_body);
                return;
            }
            Ok(x) => x,
        };

        let body = &packet_body[preamble.encoded_len()..];

        let pdu_id = match PduId::try_from(preamble.pdu_id) {
            Err(e) => {
                fx_log_err!("Unknown pdu {} received {:#?}: {:?}", preamble.pdu_id, body, e);
                let _ = command.send_response(AvcResponseType::NotImplemented, packet_body);
                return;
            }
            Ok(x) => x,
        };

        // The only PDU that you can send a Notify on is RegisterNotification.
        if pdu_id != PduId::RegisterNotification {
            let reject_response = RejectResponse::new(&pdu_id, &StatusCode::InvalidParameter);
            let packet = reject_response.encode_packet().unwrap();
            let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
            return;
        }

        match RegisterNotificationCommand::decode(&body[..]) {
            Ok(notification_command) => match *notification_command.event_id() {
                _ => {
                    fx_log_info!(
                        "Unhandled register notification {:?}",
                        *notification_command.event_id()
                    );
                    let reject_response = RejectResponse::new(
                        &PduId::RegisterNotification,
                        &StatusCode::InvalidParameter,
                    );
                    let packet =
                        reject_response.encode_packet().expect("unable to encode rejection packet");
                    // TODO: validate this correct error code to respond in this case.
                    let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
                    return;
                }
            },
            Err(e) => {
                fx_log_err!(
                    "Unable to decode register notification command {} {:#?}: {:?}",
                    preamble.pdu_id,
                    body,
                    e
                );
                let reject_response =
                    RejectResponse::new(&PduId::RegisterNotification, &StatusCode::InvalidCommand);
                let packet =
                    reject_response.encode_packet().expect("unable to encode rejection packet");
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
                return;
            }
        }
    }

    fn handle_status_vendor_command(
        &self,
        pdu_id: PduId,
        body: &[u8],
    ) -> Result<(AvcResponseType, Vec<u8>), Error> {
        match pdu_id {
            PduId::GetCapabilities => {
                if let Ok(get_cap_cmd) = GetCapabilitiesCommand::decode(body) {
                    fx_vlog!(tag: "avrcp", 2, "Received GetCapabilities Command {:#?}", get_cap_cmd);

                    match get_cap_cmd.capability_id() {
                        GetCapabilitiesCapabilityId::CompanyId => {
                            let response = GetCapabilitiesResponse::new_btsig_company();
                            let buf =
                                response.encode_packet().map_err(|e| Error::PacketError(e))?;
                            Ok((AvcResponseType::ImplementedStable, buf))
                        }
                        GetCapabilitiesCapabilityId::EventsId => {
                            let response = GetCapabilitiesResponse::new_events(&[]);
                            let buf =
                                response.encode_packet().map_err(|e| Error::PacketError(e))?;
                            Ok((AvcResponseType::ImplementedStable, buf))
                        }
                    }
                } else {
                    fx_vlog!(tag: "avrcp", 2, "Unable to parse GetCapabilitiesCommand, sending rejection.");
                    let response = RejectResponse::new(&pdu_id, &StatusCode::InvalidParameter);
                    let buf = response.encode_packet().map_err(|e| Error::PacketError(e))?;
                    Ok((AvcResponseType::Rejected, buf))
                }
            }
            PduId::GetElementAttributes => {
                let get_element_attrib_command =
                    GetElementAttributesCommand::decode(body).map_err(|e| Error::PacketError(e))?;

                fx_vlog!(tag: "avrcp", 2, "Received GetElementAttributes Command {:#?}", get_element_attrib_command);
                let response = GetElementAttributesResponse {
                    title: Some("Hello world".to_string()),
                    ..GetElementAttributesResponse::default()
                };
                let buf = response.encode_packet().map_err(|e| Error::PacketError(e))?;
                Ok((AvcResponseType::ImplementedStable, buf))
            }
            _ => {
                fx_vlog!(tag: "avrcp", 2, "Received unhandled vendor command {:?}", pdu_id);
                Err(Error::CommandNotSupported)
            }
        }
    }

    fn handle_vendor_command(
        &self,
        remote_peer: &Arc<RemotePeer>,
        command: &AvcCommand,
        pmi: &Arc<PeerManagerInner>,
    ) -> Result<(), Error> {
        let packet_body = command.body();
        let preamble = match VendorDependentPreamble::decode(packet_body) {
            Err(e) => {
                fx_log_info!(
                    "Unable to parse vendor dependent preamble {}: {:?}",
                    remote_peer.peer_id,
                    e
                );
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::NotImplemented, &packet_body[..]);
                fx_vlog!(tag: "avrcp", 2, "Sent NotImplemented response to unparsable command");
                return Ok(());
            }
            Ok(x) => x,
        };
        let body = &packet_body[preamble.encoded_len()..];
        let pdu_id = match PduId::try_from(preamble.pdu_id) {
            Err(e) => {
                fx_log_err!(
                    "Unsupported vendor dependent command pdu {} received from peer {} {:#?}: {:?}",
                    preamble.pdu_id,
                    remote_peer.peer_id,
                    body,
                    e
                );
                // recoverable error
                let preamble = VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                let prelen = preamble.encoded_len();
                let mut buf = vec![0; prelen];
                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::NotImplemented, &buf[..]);
                fx_vlog!(tag: "avrcp", 2, "Sent NotImplemented response to unsupported command {:?}", preamble.pdu_id);
                return Ok(());
            }
            Ok(x) => x,
        };
        fx_vlog!(tag: "avrcp", 2, "Received command PDU {:#?}", pdu_id);
        match command.avc_header().packet_type() {
            AvcPacketType::Command(AvcCommandType::Notify) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=notify command {:?}", pdu_id);
                self.handle_notify_vendor_command(pmi, &command);
                Ok(())
            }
            AvcPacketType::Command(AvcCommandType::Status) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=status command {:?}", pdu_id);
                // FIXME: handle_status_vendor_command should better match
                //        handle_notify_vendor_command by sending it's own responses instead
                //        of delegating it back up here to send. Originally it was done this
                //        way so that it would be easier to test but behavior difference is
                //        more confusing than the testability convenience
                match self.handle_status_vendor_command(pdu_id, &body[..]) {
                    Ok((response_type, buf)) => {
                        if let Err(e) = command.send_response(response_type, &buf[..]) {
                            fx_log_err!(
                                "Error sending vendor response to peer {}, {:?}",
                                remote_peer.peer_id,
                                e
                            );
                            // unrecoverable
                            return Err(Error::from(e));
                        }
                        fx_vlog!(tag: "avrcp", 2, "sent response {:?} to command {:?}", response_type, pdu_id);
                        Ok(())
                    }
                    Err(e) => {
                        fx_log_err!(
                            "Error parsing command packet from peer {}, {:?}",
                            remote_peer.peer_id,
                            e
                        );

                        let response_error_code: StatusCode = match e {
                            Error::CommandNotSupported => {
                                let preamble =
                                    VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                                let prelen = preamble.encoded_len();
                                let mut buf = vec![0; prelen];
                                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                                if let Err(e) =
                                    command.send_response(AvcResponseType::NotImplemented, &buf[..])
                                {
                                    fx_log_err!(
                                        "Error sending not implemented response to peer {}, {:?}",
                                        remote_peer.peer_id,
                                        e
                                    );
                                    return Err(Error::from(e));
                                }
                                fx_vlog!(tag: "avrcp", 2, "sent not implemented response to command {:?}", pdu_id);
                                return Ok(());
                            }
                            Error::PacketError(PacketError::OutOfRange) => {
                                StatusCode::InvalidParameter
                            }
                            Error::PacketError(PacketError::InvalidHeader) => {
                                StatusCode::ParameterContentError
                            }
                            Error::PacketError(PacketError::InvalidMessage) => {
                                StatusCode::ParameterContentError
                            }
                            Error::PacketError(PacketError::UnsupportedMessage) => {
                                StatusCode::InternalError
                            }
                            _ => StatusCode::InternalError,
                        };

                        let pdu_id = PduId::try_from(preamble.pdu_id).expect("Handled above.");
                        let reject_response = RejectResponse::new(&pdu_id, &response_error_code);
                        if let Ok(packet) = reject_response.encode_packet() {
                            if let Err(e) =
                                command.send_response(AvcResponseType::Rejected, &packet[..])
                            {
                                fx_log_err!(
                                    "Error sending vendor reject response to peer {}, {:?}",
                                    remote_peer.peer_id,
                                    e
                                );
                                return Err(Error::from(e));
                            }
                            fx_vlog!(tag: "avrcp", 2, "sent not reject response {:?} to command {:?}", response_error_code, pdu_id);
                        } else {
                            // TODO(BT-2220): Audit behavior. Consider different options in this
                            //                error case like dropping the L2CAP socket.
                            fx_log_err!("Unable to encoded reject response. dropping.");
                        }

                        Ok(())
                    }
                }
            }
            _ => {
                let preamble = VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                let prelen = preamble.encoded_len();
                let mut buf = vec![0; prelen];
                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                let _ = command.send_response(AvcResponseType::NotImplemented, &buf[..]);
                fx_vlog!(tag: "avrcp", 2, "unexpected ctype: {:?}. responding with not implemented {:?}", command.avc_header().packet_type(), pdu_id);
                Ok(())
            }
        }
    }

    fn handle_command(&self, command: AvcCommand, pmi: Arc<PeerManagerInner>) -> Result<(), Error> {
        match Weak::upgrade(&self.remote_peer) {
            Some(remote_peer) => {
                fx_vlog!(tag: "avrcp", 2, "received command {:#?}", command);
                match command.avc_header().op_code() {
                    &AvcOpCode::VendorDependent => {
                        self.handle_vendor_command(&remote_peer, &command, &pmi)
                    }
                    &AvcOpCode::Passthrough => {
                        match self.handle_passthrough_command(&remote_peer, &command) {
                            Ok(response_type) => {
                                if let Err(e) = command.send_response(response_type, &[]) {
                                    fx_log_err!(
                                        "Unable to send passthrough response to peer {}, {:?}",
                                        remote_peer.peer_id,
                                        e
                                    );
                                    return Err(Error::from(e));
                                }
                                fx_vlog!(tag: "avrcp", 2, "sent response {:?} to passthrough command", response_type);
                                Ok(())
                            }
                            Err(e) => {
                                fx_log_err!(
                                    "Error parsing command packet from peer {}, {:?}",
                                    remote_peer.peer_id,
                                    e
                                );

                                // Sending the error response. This is best effort since the peer
                                // has sent us malformed packet, so we are ignoring and purposefully
                                // not handling and error received to us sending a rejection response.
                                // TODO(BT-2220): audit this behavior and consider logging.
                                let _ = command.send_response(AvcResponseType::Rejected, &[]);
                                fx_vlog!(tag: "avrcp", 2, "sent reject response to passthrough command: {:?}", command);
                                Ok(())
                            }
                        }
                    }
                    _ => {
                        fx_vlog!(tag: "avrcp", 2, "unexpected on command packet: {:?}", command.avc_header().op_code());
                        Ok(())
                    }
                }
            }
            None => panic!("Unexpected state. remote peer should not be deallocated"),
        }
    }
}

/// NotificationStream returns each INTERIM response for a given NotificationEventId on a peer.
///
/// Internally this stream sends the NOTIFY RegisterNotification command with the supplied the event
/// ID. Upon receiving an interim value it will yield back the value on the stream. It will then
/// wait for a changed event or some other expected event to occur that signals the value has
/// changed and will re-register for to receive the next interim value to yield back in a loop.
///
/// The stream will terminate if an unexpected error/response is received by the peer or the peer
/// connection is closed.
struct NotificationStream {
    peer: Arc<RemotePeer>,
    event_id: NotificationEventId,
    playback_interval: u32,
    stream: Option<Pin<Box<dyn Stream<Item = Result<AvcCommandResponse, AvctpError>> + Send>>>,
    terminated: bool,
}

impl NotificationStream {
    fn new(peer: Arc<RemotePeer>, event_id: NotificationEventId, playback_interval: u32) -> Self {
        Self { peer, event_id, playback_interval, stream: None, terminated: false }
    }

    fn setup_stream(
        &self,
    ) -> Result<impl Stream<Item = Result<AvcCommandResponse, AvctpError>> + Send, Error> {
        let command = if self.event_id == NotificationEventId::EventPlaybackPosChanged {
            RegisterNotificationCommand::new_position_changed(self.playback_interval)
        } else {
            RegisterNotificationCommand::new(self.event_id)
        };
        let conn = self.peer.control_channel.read().connection().ok_or(Error::RemoteNotFound)?;
        let packet = command.encode_packet().expect("unable to encode packet");
        let stream = conn
            .send_vendor_dependent_command(AvcCommandType::Notify, &packet[..])
            .map_err(|e| Error::from(e))?;
        Ok(stream)
    }
}

impl FusedStream for NotificationStream {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

impl Stream for NotificationStream {
    type Item = Result<Vec<u8>, Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            return Poll::Ready(None);
        }

        loop {
            if self.stream.is_none() {
                match self.setup_stream() {
                    Ok(stream) => self.stream = Some(stream.boxed()),
                    Err(e) => {
                        self.terminated = true;
                        return Poll::Ready(Some(Err(e)));
                    }
                }
            }

            let stream = self.stream.as_mut().expect("stream should not be none").as_mut();
            let result = ready!(stream.poll_next(cx));
            let return_result = match result {
                Some(Ok(response)) => {
                    fx_vlog!(tag: "avrcp", 2, "received event response {:?}", response);
                    // We ignore the "changed" event and just use it to let use requeue a new
                    // register notification. We will then just use the interim response of the next
                    // command to prevent duplicates. "rejected" with the appropriate status code
                    // after interim typically happen when the player has changed so we re-prime the
                    // notification again just like a changed event.
                    match response.response_type() {
                        AvcResponseType::Interim => Ok(Some(response.response().to_vec())),
                        AvcResponseType::NotImplemented => Err(Error::CommandNotSupported),
                        AvcResponseType::Rejected => {
                            let body = response.response();
                            match VendorDependentPreamble::decode(&body[..]) {
                                Ok(reject_packet) => {
                                    let payload = &body[reject_packet.encoded_len()..];
                                    if payload.len() > 0 {
                                        match StatusCode::try_from(payload[0]) {
                                            Ok(status_code) => match status_code {
                                                StatusCode::AddressedPlayerChanged => {
                                                    self.stream = None;
                                                    continue;
                                                }
                                                StatusCode::InvalidCommand => {
                                                    Err(Error::CommandFailed)
                                                }
                                                StatusCode::InvalidParameter => {
                                                    Err(Error::CommandNotSupported)
                                                }
                                                StatusCode::InternalError => {
                                                    Err(Error::GenericError(format_err!(
                                                        "Remote internal error"
                                                    )))
                                                }
                                                _ => Err(Error::UnexpectedResponse),
                                            },
                                            Err(_) => Err(Error::UnexpectedResponse),
                                        }
                                    } else {
                                        Err(Error::UnexpectedResponse)
                                    }
                                }
                                Err(_) => Err(Error::UnexpectedResponse),
                            }
                        }
                        AvcResponseType::Changed => {
                            // Repump.
                            self.stream = None;
                            continue;
                        }
                        // All others are invalid responses for register notification.
                        _ => Err(Error::UnexpectedResponse),
                    }
                }
                Some(Err(e)) => Err(Error::from(e)),
                None => Ok(None),
            };

            break match return_result {
                Ok(Some(data)) => Poll::Ready(Some(Ok(data))),
                Ok(None) => {
                    self.terminated = true;
                    Poll::Ready(None)
                }
                Err(err) => {
                    self.terminated = true;
                    Poll::Ready(Some(Err(err)))
                }
            };
        }
    }
}
