// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use futures::ready;

/// NotificationStream returns each INTERIM response for a given NotificationEventId on a peer.
///
/// This will register for the notification with the peer and produce elements whenever the event
/// has a new value, including the initial value. It will re-register for notifications on the
/// event as long as the peer is connected and the stream is not dropped.
///
/// The stream will terminate if an unexpected error/response is received by the peer.
pub struct NotificationStream {
    peer: Arc<RwLock<RemotePeer>>,
    event_id: NotificationEventId,
    playback_interval: u32,
    stream: Option<Pin<Box<dyn Stream<Item = Result<AvcCommandResponse, AvctpError>> + Send>>>,
    terminated: bool,
}

impl NotificationStream {
    // Only the task processing peer notifications should construct new notification streams to
    // avoid transaction id exhaustion.
    pub(super) fn new(
        peer: Arc<RwLock<RemotePeer>>,
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
        let conn = self.peer.write().control_connection()?;
        let packet = command.encode_packet().expect("unable to encode packet");
        let stream = conn
            .send_vendor_dependent_command(AvcCommandType::Notify, &packet[..])
            .map_err(|e| Error::from(e))?;
        Ok(stream)
    }

    fn handle_response(&mut self, response: AvcCommandResponse) -> Result<Vec<u8>, Error> {
        // We ignore the "changed" event and just use it to let use requeue a new
        // register notification. We will then just use the interim response of the next
        // command to prevent duplicates. "rejected" with the appropriate status code
        // after interim typically happen when the player has changed so we re-prime the
        // notification again just like a changed event.
        match response.response_type() {
            AvcResponseType::Interim => Ok(response.response().to_vec()),
            AvcResponseType::NotImplemented => Err(Error::CommandNotSupported),
            AvcResponseType::Rejected => {
                let body = response.response();
                let reject_packet = VendorDependentPreamble::decode(&body[..])
                    .or(Err(Error::UnexpectedResponse))?;
                let payload = &body[reject_packet.encoded_len()..];
                if payload.len() == 0 {
                    return Err(Error::UnexpectedResponse);
                }
                match StatusCode::try_from(payload[0]).or(Err(Error::UnexpectedResponse))? {
                    StatusCode::AddressedPlayerChanged => {
                        // Return value is ignored, we will re-register the notificaton.
                        self.stream = None;
                        return Err(Error::UnexpectedResponse);
                    }
                    StatusCode::InvalidCommand => Err(Error::CommandFailed),
                    StatusCode::InvalidParameter => Err(Error::CommandNotSupported),
                    StatusCode::InternalError => {
                        Err(Error::GenericError(format_err!("Remote internal error")))
                    }
                    _ => Err(Error::UnexpectedResponse),
                }
            }
            AvcResponseType::Changed => {
                // Return value is ignored, we will re-register the notification
                self.stream = None;
                return Err(Error::UnexpectedResponse);
            }
            // All others are invalid responses for register notification.
            _ => Err(Error::UnexpectedResponse),
        }
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
                    match self.handle_response(response) {
                        Ok(response) => Ok(Some(response)),
                        Err(e) => {
                            // If we need to re-register
                            if self.stream.is_none() {
                                continue;
                            }
                            Err(e)
                        }
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
