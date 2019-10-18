// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

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
    pub peer_id: PeerId,

    /// Contains the remote peer's target profile.
    pub target_descriptor: RwLock<Option<AvrcpService>>,

    /// Contains the remote peer's controller profile.
    pub controller_descriptor: RwLock<Option<AvrcpService>>,

    /// Control channel to the remote device.
    pub control_channel: RwLock<PeerChannel<AvcPeer>>,

    // TODO(BT-2221): add browse channel.
    // browse_channel: RwLock<PeerChannel<AvtcpPeer>>,
    //
    /// Contains a vec of all PeerControllers that have taken an event stream waiting for events from this peer.
    pub controller_listeners: Mutex<Vec<mpsc::Sender<ControllerEvent>>>,

    /// Processes commands received as AVRCP target and holds state for continuations and requested
    /// notifications for the control channel. Only set once we have enough information to determine
    /// our role based on the peer's SDP record.
    pub command_handler: Mutex<Option<ControlChannelCommandHandler>>,
}

impl RemotePeer {
    pub fn new(peer_id: PeerId) -> Self {
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
    pub fn broadcast_event(&self, event: ControllerEvent) {
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
    pub fn reset_command_handler(&self) {
        let mut cmd_handler = self.command_handler.lock();
        *cmd_handler = None;
    }

    pub fn reset_connection(&self) {
        let mut control_channel = self.control_channel.write();
        self.reset_command_handler();
        *control_channel = PeerChannel::Disconnected;
    }

    /// Send a generic "status" vendor dependent command and returns the result as a future.
    /// This method encodes the `command` packet, awaits and decodes all responses, will issue
    /// continuation commands for incomplete responses (eg "get_element_attributes" command), and
    /// will return a result of the decoded packet or an error for any non stable response received
    pub async fn send_status_vendor_dependent_command<'a>(
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

    pub async fn send_avc_passthrough_keypress(&self, avc_keycode: u8) -> Result<(), Error> {
        {
            // key_press
            let payload_1 = &[avc_keycode, 0x00];
            let r = self.control_channel.read().connection();
            if let Some(peer) = r {
                let response = peer.send_avc_passthrough_command(payload_1).await;
                match response {
                    Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {}
                    Ok(AvcCommandResponse(AvcResponseType::NotImplemented, _)) => {
                        return Err(Error::CommandNotSupported);
                    }
                    Err(e) => {
                        fx_log_err!("error sending avc command to {}: {:?}", self.peer_id, e);
                        return Err(Error::CommandFailed);
                    }
                    Ok(response) => {
                        fx_log_err!(
                            "error sending avc command. unhandled response {}: {:?}",
                            self.peer_id,
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
            let r = self.control_channel.read().connection();
            if let Some(peer) = r {
                let response = peer.send_avc_passthrough_command(payload_2).await;
                match response {
                    Ok(AvcCommandResponse(AvcResponseType::Accepted, _)) => {
                        return Ok(());
                    }
                    Ok(AvcCommandResponse(AvcResponseType::Rejected, _)) => {
                        fx_log_info!("avrcp command rejected {}: {:?}", self.peer_id, response);
                        return Err(Error::CommandNotSupported);
                    }
                    Err(e) => {
                        fx_log_err!("error sending avc command to {}: {:?}", self.peer_id, e);
                        return Err(Error::CommandFailed);
                    }
                    _ => {
                        fx_log_err!(
                            "error sending avc command. unhandled response {}: {:?}",
                            self.peer_id,
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

    pub async fn set_absolute_volume(&self, volume: u8) -> Result<u8, Error> {
        let conn = self.control_channel.read().connection();
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

    pub async fn get_media_attributes(&self) -> Result<MediaAttributes, Error> {
        let conn = self.control_channel.read().connection();
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

    pub async fn get_supported_events(&self) -> Result<Vec<NotificationEventId>, Error> {
        let conn = self.control_channel.read().connection();
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

/// NotificationStream returns each INTERIM response for a given NotificationEventId on a peer.
///
/// Internally this stream sends the NOTIFY RegisterNotification command with the supplied the event
/// ID. Upon receiving an interim value it will yield back the value on the stream. It will then
/// wait for a changed event or some other expected event to occur that signals the value has
/// changed and will re-register for to receive the next interim value to yield back in a loop.
///
/// The stream will terminate if an unexpected error/response is received by the peer or the peer
/// connection is closed.
pub struct NotificationStream {
    peer: Arc<RemotePeer>,
    event_id: NotificationEventId,
    playback_interval: u32,
    stream: Option<Pin<Box<dyn Stream<Item = Result<AvcCommandResponse, AvctpError>> + Send>>>,
    terminated: bool,
}

impl NotificationStream {
    pub fn new(
        peer: Arc<RemotePeer>,
        event_id: NotificationEventId,
        playback_interval: u32,
    ) -> Self {
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
