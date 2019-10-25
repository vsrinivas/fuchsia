// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

#[derive(Debug, Clone)]
pub enum ControllerEvent {
    PlaybackStatusChanged(PlaybackStatus),
    TrackIdChanged(u64),
    PlaybackPosChanged(u32),
}

pub type ControllerEventStream = mpsc::Receiver<ControllerEvent>;

/// Controller interface for a remote peer returned by the PeerManager using the
/// ControllerRequest stream for a given ControllerRequest.
#[derive(Debug)]
pub struct Controller {
    peer: Arc<RemotePeer>,
}

impl Controller {
    pub fn new(peer: Arc<RemotePeer>) -> Controller {
        Controller { peer }
    }

    pub async fn send_avc_passthrough_keypress(&self, avc_keycode: u8) -> Result<(), Error> {
        self.peer.send_avc_passthrough_keypress(avc_keycode).await
    }

    pub async fn set_absolute_volume(&self, requested_volume: u8) -> Result<u8, Error> {
        self.peer.set_absolute_volume(requested_volume).await
    }

    pub async fn get_media_attributes(&self) -> Result<MediaAttributes, Error> {
        self.peer.get_media_attributes().await
    }

    pub async fn get_supported_events(&self) -> Result<Vec<NotificationEventId>, Error> {
        self.peer.get_supported_events().await
    }

    pub async fn send_raw_vendor_command<'a>(
        &'a self,
        pdu_id: u8,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, Error> {
        let command = RawVendorDependentPacket::new(PduId::try_from(pdu_id)?, payload);
        let connection = self.peer.control_channel.read().connection();
        match connection {
            Some(peer) => RemotePeer::send_status_vendor_dependent_command(&peer, &command).await,
            _ => Err(Error::RemoteNotFound),
        }
    }

    /// For the FIDL test controller. Informational only and intended for logging only. The state is
    /// inherently racey.
    pub fn is_connected(&self) -> bool {
        let connection = self.peer.control_channel.read();
        match *connection {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    /// Returns notification events from the peer.
    pub fn take_event_stream(&self) -> ControllerEventStream {
        let (sender, receiver) = mpsc::channel(2);
        self.peer.controller_listeners.lock().push(sender);
        receiver
    }
}
