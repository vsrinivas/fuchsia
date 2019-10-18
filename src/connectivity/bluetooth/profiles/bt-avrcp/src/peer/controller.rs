// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

/// Controller interface for a remote peer returned by the PeerManager using the
/// PeerControllerRequest stream for a given PeerControllerRequest.
#[derive(Debug)]
pub struct PeerController {
    pub(super) peer: Arc<RemotePeer>,
}

impl PeerController {
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

    /// Informational only. Intended for logging only. Inherently racey.
    pub fn is_connected(&self) -> bool {
        let connection = self.peer.control_channel.read();
        match *connection {
            PeerChannel::Connected(_) => true,
            _ => false,
        }
    }

    pub fn take_event_stream(&self) -> PeerControllerEventStream {
        let (sender, receiver) = mpsc::channel(512);
        self.peer.controller_listeners.lock().push(sender);
        receiver
    }
}
