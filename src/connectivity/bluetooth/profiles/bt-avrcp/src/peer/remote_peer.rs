// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

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
    pub controller_listeners: Mutex<Vec<mpsc::Sender<PeerControllerEvent>>>,

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
    pub fn broadcast_event(&self, event: PeerControllerEvent) {
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
