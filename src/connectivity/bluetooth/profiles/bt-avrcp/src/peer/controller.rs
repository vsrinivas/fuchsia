// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

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
    pub(super) inner: Arc<PeerManagerInner>,
    pub(super) peer_id: PeerId,
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
