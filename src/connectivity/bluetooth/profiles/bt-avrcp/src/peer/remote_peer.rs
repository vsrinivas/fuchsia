// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::{packets::VendorCommand, types::StateChangeListener};

use futures::{future::AbortHandle, future::Abortable};

pub type RemotePeerHandle = RwLock<RemotePeer>;

#[derive(Debug, PartialEq)]
pub enum PeerChannel<T> {
    Connected(Arc<T>),
    Connecting,
    Disconnected,
}

impl<T> PeerChannel<T> {
    pub fn connection(&self) -> Option<Arc<T>> {
        match self {
            PeerChannel::Connected(t) => Some(t.clone()),
            _ => None,
        }
    }
}

/// Internal object to manage a remote peer
#[derive(Debug)]
pub struct RemotePeer {
    peer_id: PeerId,

    /// Contains the remote peer's target profile.
    target_descriptor: Option<AvrcpService>,

    /// Contains the remote peer's controller profile.
    controller_descriptor: Option<AvrcpService>,

    /// Control channel to the remote device.
    control_channel: PeerChannel<AvcPeer>,

    /// Profile service. Used by RemotePeer to make outgoing L2CAP connections.
    profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,

    // TODO(BT-2221): add browse channel.
    // browse_channel: PeerChannel<AvtcpPeer>,
    //
    /// Contains a vec of all event stream listeners obtained by any Controllers around this peer
    /// that are listening for events from this peer from this peer.
    controller_listeners: Vec<mpsc::Sender<ControllerEvent>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel.
    command_handler: Arc<Mutex<ControlChannelHandler>>,

    state_change_listener: StateChangeListener,

    /// set after before waking the state change listener to have it valid for the next check to
    /// attempt an outgoing l2cap connection
    attempt_connection: bool,
}

impl RemotePeer {
    pub fn new(
        peer_id: PeerId,
        target_delegate: Arc<TargetDelegate>,
        profile_svc: Arc<Box<dyn ProfileService + Send + Sync>>,
    ) -> Arc<RemotePeerHandle> {
        Arc::new(RwLock::new(Self {
            peer_id: peer_id.clone(),
            target_descriptor: None,
            controller_descriptor: None,
            control_channel: PeerChannel::Disconnected,
            // TODO(BT-2221): add browse channel.
            //browse_channel: PeerChannel::Disconnected,
            controller_listeners: Vec::new(),
            profile_svc,
            command_handler: Arc::new(Mutex::new(ControlChannelHandler::new(
                &peer_id,
                target_delegate,
            ))),
            state_change_listener: StateChangeListener::new(),
            attempt_connection: true,
        }))
    }

    /// Enumerates all listening controller_listeners queues and sends a clone of the event to each
    fn broadcast_event(&mut self, event: ControllerEvent) {
        // remove all the dead listeners from the list.
        self.controller_listeners.retain(|i| !i.is_closed());
        for sender in self.controller_listeners.iter_mut() {
            if let Err(send_error) = sender.try_send(event.clone()) {
                fx_log_err!(
                    "unable to send event to peer controller stream for {} {:?}",
                    self.peer_id,
                    send_error
                );
            }
        }
    }

    pub fn add_control_listener(this: &RemotePeerHandle, sender: mpsc::Sender<ControllerEvent>) {
        let mut peer_guard = this.write();
        peer_guard.controller_listeners.push(sender)
    }

    fn connected(&self) -> bool {
        match self.control_channel {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    pub fn is_connected(this: &RemotePeerHandle) -> bool {
        this.read().connected()
    }

    fn reset_command_handler(&mut self) {
        fx_vlog!(tag: "avrcp", 2, "reset_command_handler {:?}", self.peer_id);
        self.command_handler.lock().reset();
    }

    /// `attempt_connection` will cause state_watcher to attempt to make an outgoing connection when
    /// woken.
    fn reset_connection(&mut self, attempt_connection: bool) {
        fx_vlog!(tag: "avrcp", 2, "reset_connection {:?}", self.peer_id);
        self.reset_command_handler();
        self.control_channel = PeerChannel::Disconnected;
        self.attempt_connection = attempt_connection;
        self.wake_state_watcher();
    }

    fn control_connection(&mut self) -> Result<Arc<AvcPeer>, Error> {
        // if we are not connected, try to reconnect the next time we want to send a command.
        if !self.connected() {
            self.attempt_connection = true;
            self.wake_state_watcher();
        }

        self.control_channel.connection().ok_or(Error::RemoteNotFound)
    }

    pub fn get_control_connection(this: &RemotePeerHandle) -> Result<Arc<AvcPeer>, Error> {
        this.write().control_connection()
    }

    fn set_control_connection_internal(&mut self, peer: AvcPeer) {
        fx_vlog!(tag: "avrcp", 2, "set_control_connection {:?}", self.peer_id);
        self.reset_command_handler();
        self.control_channel = PeerChannel::Connected(Arc::new(peer));
        self.wake_state_watcher();
    }

    pub fn set_control_connection(this: &RemotePeerHandle, peer: AvcPeer) {
        this.write().set_control_connection_internal(peer);
    }

    pub fn set_target_descriptor_internal(&mut self, service: AvrcpService) {
        fx_vlog!(tag: "avrcp", 2, "set_target_descriptor {:?}", self.peer_id);
        self.target_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    pub fn set_target_descriptor(this: &RemotePeerHandle, service: AvrcpService) {
        this.write().set_target_descriptor_internal(service);
    }

    pub fn set_controller_descriptor_internal(&mut self, service: AvrcpService) {
        fx_vlog!(tag: "avrcp", 2, "set_controller_descriptor {:?}", self.peer_id);
        self.controller_descriptor = Some(service);
        self.attempt_connection = true;
        self.wake_state_watcher();
    }

    pub fn set_controller_descriptor(this: &RemotePeerHandle, service: AvrcpService) {
        this.write().set_controller_descriptor_internal(service);
    }

    fn handle_notification(
        notif: &NotificationEventId,
        peer: &Arc<RemotePeerHandle>,
        data: &[u8],
    ) -> Result<bool, Error> {
        fx_vlog!(tag: "avrcp", 2, "received notification for {:?} {:?}", notif, data);

        let preamble = VendorDependentPreamble::decode(data).map_err(|e| Error::PacketError(e))?;

        let data = &data[preamble.encoded_len()..];

        if data.len() < preamble.parameter_length as usize {
            return Err(Error::UnexpectedResponse);
        }

        match notif {
            NotificationEventId::EventPlaybackStatusChanged => {
                let response = PlaybackStatusChangedNotificationResponse::decode(data)
                    .map_err(|e| Error::PacketError(e))?;
                peer.write().broadcast_event(ControllerEvent::PlaybackStatusChanged(
                    response.playback_status(),
                ));
                Ok(false)
            }
            NotificationEventId::EventTrackChanged => {
                let response = TrackChangedNotificationResponse::decode(data)
                    .map_err(|e| Error::PacketError(e))?;
                peer.write()
                    .broadcast_event(ControllerEvent::TrackIdChanged(response.identifier()));
                Ok(false)
            }
            NotificationEventId::EventPlaybackPosChanged => {
                let response = PlaybackPosChangedNotificationResponse::decode(data)
                    .map_err(|e| Error::PacketError(e))?;
                peer.write()
                    .broadcast_event(ControllerEvent::PlaybackPosChanged(response.position()));
                Ok(false)
            }
            _ => Ok(true),
        }
    }

    fn make_connection(peer: Arc<RemotePeerHandle>) {
        let peer = peer.clone();

        fasync::spawn(async move {
            let (connecting, peer_id, profile_service) = {
                let peer_guard = peer.read();
                (
                    match peer_guard.control_channel {
                        PeerChannel::Connecting => true,
                        _ => false,
                    },
                    peer_guard.peer_id.clone(),
                    peer_guard.profile_svc.clone(),
                )
            };
            if connecting {
                match profile_service.connect_to_device(&peer_id, PSM_AVCTP as u16).await {
                    Ok(socket) => {
                        {
                            let mut peer_guard = peer.write();
                            match peer_guard.control_channel {
                                PeerChannel::Connecting => match AvcPeer::new(socket) {
                                    Ok(peer) => {
                                        peer_guard.set_control_connection_internal(peer);
                                    }
                                    Err(e) => {
                                        peer_guard.reset_connection(false);
                                        fx_log_err!(
                                            "Unable to make peer from socket {}: {:?}",
                                            peer_id,
                                            e
                                        );
                                    }
                                },
                                _ => {
                                    fx_log_info!(
                                            "incoming connection established while making outgoing {:?}",
                                            peer_id
                                        );

                                    // an incoming l2cap connection was made while we were making an
                                    // outgoing one. Drop both connections per spec.
                                    peer_guard.reset_connection(false);
                                }
                            };
                        }
                    }
                    Err(e) => {
                        fx_log_err!("connect_to_device error {}: {:?}", peer_id, e);
                        let mut peer_guard = peer.write();
                        if let PeerChannel::Connecting = peer_guard.control_channel {
                            peer_guard.reset_connection(false);
                        }
                    }
                }
            }
        })
    }

    async fn pump_notifications(peer: Arc<RemotePeerHandle>) {
        // events we support when speaking to a peer that supports the target profile.
        const SUPPORTED_NOTIFICATIONS: [NotificationEventId; 3] = [
            NotificationEventId::EventPlaybackStatusChanged,
            NotificationEventId::EventTrackChanged,
            NotificationEventId::EventPlaybackPosChanged,
        ];

        let supported_notifications: Vec<NotificationEventId> =
            SUPPORTED_NOTIFICATIONS.iter().cloned().collect();

        // look up what notifications we support on this peer first
        let remote_supported_notifications = match Self::get_supported_events(peer.clone()).await {
            Ok(x) => x,
            Err(_) => return,
        };

        let supported_notifications: Vec<NotificationEventId> = remote_supported_notifications
            .into_iter()
            .filter(|k| supported_notifications.contains(k))
            .collect();

        let mut notification_streams = SelectAll::new();

        for notif in supported_notifications {
            fx_vlog!(tag: "avrcp", 2, "creating notification stream for {:#?}", notif);
            let stream =
                NotificationStream::new(peer.clone(), notif, 5).map_ok(move |data| (notif, data));
            notification_streams.push(stream);
        }

        pin_mut!(notification_streams);
        loop {
            if futures::select! {
                event_result = notification_streams.select_next_some() => {
                    match event_result {
                        Ok((notif, data)) => {
                            Self::handle_notification(&notif, &peer, &data[..])
                                .unwrap_or_else(|e| { fx_log_err!("Error decoding packet from peer {:?}", e); true} )
                        },
                        Err(Error::CommandNotSupported) => false,
                        Err(_) => true,
                        _=> true,
                    }
                }
                complete => { true }
            } {
                break;
            }
        }
        fx_vlog!(tag: "avrcp", 2, "stopping notifications for {:#?}", peer.read().peer_id);
    }

    fn wake_state_watcher(&self) {
        fx_vlog!(tag: "avrcp", 2, "wake_state_watcher {:?}", self.peer_id);
        self.state_change_listener.state_changed();
    }

    pub async fn state_watcher(peer: Arc<RemotePeerHandle>) {
        fx_vlog!(tag: "avrcp", 2, "state_watcher starting");
        let mut change_stream = peer.read().state_change_listener.take_change_stream();
        let peer_weak = Arc::downgrade(&peer);
        drop(peer);

        let mut channel_processor_abort_handle: Option<AbortHandle> = None;
        let mut notification_poll_abort_handle: Option<AbortHandle> = None;

        while let Some(_) = change_stream.next().await {
            fx_vlog!(tag: "avrcp", 2, "state_watcher command received");
            if let Some(peer) = peer_weak.upgrade() {
                let mut peer_guard = peer.write();

                fx_vlog!(tag: "avrcp", 2, "make_connection control channel {:?}", peer_guard.control_channel);
                match peer_guard.control_channel {
                    PeerChannel::Connecting => {}
                    PeerChannel::Disconnected => {
                        if let Some(ref abort_handle) = channel_processor_abort_handle {
                            abort_handle.abort();
                            channel_processor_abort_handle = None;
                        }

                        if let Some(ref abort_handle) = notification_poll_abort_handle {
                            abort_handle.abort();
                            notification_poll_abort_handle = None;
                        }

                        // Have we discovered service profile data on the peer?
                        if (peer_guard.target_descriptor.is_some()
                            || peer_guard.controller_descriptor.is_some())
                            && peer_guard.attempt_connection
                        {
                            fx_vlog!(tag: "avrcp", 2, "make_connection {:?}", peer_guard.peer_id);
                            peer_guard.attempt_connection = false;
                            peer_guard.control_channel = PeerChannel::Connecting;
                            Self::make_connection(peer.clone());
                        }
                    }
                    PeerChannel::Connected(_) => {
                        // Have we discovered service profile data on the peer?
                        if (peer_guard.target_descriptor.is_some()
                            || peer_guard.controller_descriptor.is_some())
                            && channel_processor_abort_handle.is_none()
                        {
                            channel_processor_abort_handle =
                                Some(Self::start_processing_peer_channels(peer.clone()));
                        }

                        if peer_guard.target_descriptor.is_some()
                            && notification_poll_abort_handle.is_none()
                        {
                            notification_poll_abort_handle =
                                Some(Self::start_processing_peer_notifications(peer.clone()));
                        }
                    }
                }
            } else {
                break;
            }
        }

        fx_vlog!(tag: "avrcp", 2, "state_watcher shutting down. aborting processors");

        // Stop processing state changes entirely on the peer.
        if let Some(ref abort_handle) = channel_processor_abort_handle {
            abort_handle.abort();
        }

        if let Some(ref abort_handle) = notification_poll_abort_handle {
            abort_handle.abort();
        }
    }

    async fn process_control_stream(peer: Arc<RemotePeerHandle>) {
        let (command_handler, connection) = {
            let peer_guard = peer.read();
            (peer_guard.command_handler.clone(), peer_guard.control_channel.connection())
        };

        if let Some(peer_connection) = connection {
            let mut command_stream = peer_connection.take_command_stream();

            while let Some(result) = command_stream.next().await {
                match result {
                    Ok(command) => {
                        if let Err(e) =
                            ControlChannelHandler::handle_command(command_handler.clone(), command)
                                .await
                        {
                            fx_log_info!("Command returned error from command handler {:?}", e);
                        }
                    }
                    Err(e) => {
                        fx_log_info!("Command stream returned error {:?}", e);
                        break;
                    }
                }
            }
            // command stream closed or errored. Disconnect the peer.
            {
                peer.write().reset_connection(false);
            }
        }
    }

    pub fn start_processing_peer_notifications(peer: Arc<RemotePeerHandle>) -> AbortHandle {
        let (handle, registration) = AbortHandle::new_pair();
        fasync::spawn(
            Abortable::new(
                async move {
                    Self::pump_notifications(peer).await;
                },
                registration,
            )
            .map(|_| ()),
        );
        handle
    }

    pub fn start_processing_peer_channels(peer: Arc<RemotePeerHandle>) -> AbortHandle {
        let (handle, registration) = AbortHandle::new_pair();
        fasync::spawn(
            Abortable::new(
                async move {
                    Self::process_control_stream(peer).await;
                },
                registration,
            )
            .map(|_| ()),
        );
        handle
    }

    /// Send a generic vendor dependent command and returns the result as a future.
    /// This method encodes the `command` packet, awaits and decodes all responses, will issue
    /// continuation commands for incomplete responses (eg "get_element_attributes" command), and
    /// will return a result of the decoded packet or an error for any non stable response received
    pub async fn send_vendor_dependent_command(
        peer: Arc<RemotePeerHandle>,
        command: &(impl VendorDependent + VendorCommand),
    ) -> Result<Vec<u8>, Error> {
        let avc_peer = peer.write().control_connection()?;
        let mut buf = vec![];
        let packet = command.encode_packet().expect("unable to encode packet");
        let mut stream =
            avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;

        loop {
            let response = loop {
                let result = stream.next().await.ok_or(Error::CommandFailed)?;
                let response: AvcCommandResponse = result.map_err(|e| Error::AvctpError(e))?;
                fx_vlog!(tag: "avrcp", 1, "vendor response {:#?}", response);
                match (response.response_type(), command.command_type()) {
                    (AvcResponseType::Interim, _) => continue,
                    (AvcResponseType::NotImplemented, _) => return Err(Error::CommandNotSupported),
                    (AvcResponseType::Rejected, _) => return Err(Error::CommandFailed),
                    (AvcResponseType::InTransition, _) => return Err(Error::UnexpectedResponse),
                    (AvcResponseType::Changed, _) => return Err(Error::UnexpectedResponse),
                    (AvcResponseType::Accepted, AvcCommandType::Control) => break response.1,
                    (AvcResponseType::ImplementedStable, AvcCommandType::Status) => {
                        break response.1
                    }
                    _ => return Err(Error::UnexpectedResponse),
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

            let command = RequestContinuingResponseCommand::new(u8::from(&command.pdu_id()));
            let packet = command.encode_packet().expect("unable to encode packet");

            stream = avc_peer.send_vendor_dependent_command(command.command_type(), &packet[..])?;
        }
        Ok(buf)
    }

    /// Sends a single passthrough keycode over the control channel.
    pub async fn send_avc_passthrough(
        peer: Arc<RemotePeerHandle>,
        payload: &[u8; 2],
    ) -> Result<(), Error> {
        let avc_peer = peer.write().control_connection()?;
        let response = avc_peer.send_avc_passthrough_command(payload).await;
        match response {
            Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {
                return Ok(());
            }
            Ok(AvcCommandResponse(AvcResponseType::Rejected, _)) => {
                fx_log_info!("avrcp command rejected {}: {:?}", peer.read().peer_id, response);
                return Err(Error::CommandNotSupported);
            }
            Err(e) => {
                fx_log_err!("error sending avc command to {}: {:?}", peer.read().peer_id, e);
                return Err(Error::CommandFailed);
            }
            _ => {
                fx_log_err!(
                    "error sending avc command. unhandled response {}: {:?}",
                    peer.read().peer_id,
                    response
                );
                return Err(Error::CommandFailed);
            }
        }
    }

    /// Retrieve the events supported by the peer by issuing a GetCapabilities command.
    pub async fn get_supported_events(
        peer: Arc<RemotePeerHandle>,
    ) -> Result<Vec<NotificationEventId>, Error> {
        let cmd = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
        fx_vlog!(tag: "avrcp", 1, "get_capabilities(events) send command {:#?}", cmd);
        let buf = Self::send_vendor_dependent_command(peer.clone(), &cmd).await?;
        let capabilities =
            GetCapabilitiesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        let mut event_ids = vec![];
        for event_id in capabilities.event_ids() {
            event_ids.push(NotificationEventId::try_from(event_id)?);
        }
        Ok(event_ids)
    }
}

impl Drop for RemotePeer {
    fn drop(&mut self) {
        // Stop any stream processors that are currently running on this remote peer.
        self.state_change_listener.terminate();
    }
}
