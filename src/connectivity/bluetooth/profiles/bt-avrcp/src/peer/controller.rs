// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::packets::get_play_status::{SONG_LENGTH_NOT_SUPPORTED, SONG_POSITION_NOT_SUPPORTED};

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

    /// Sends a AVC key press and key release passthrough command.
    pub async fn send_keypress(&self, avc_keycode: u8) -> Result<(), Error> {
        {
            // key_press
            let payload_1 = &[avc_keycode, 0x00];
            let _ = self.peer.send_avc_passthrough(payload_1).await?;
        }
        {
            // key_release
            let payload_2 = &[avc_keycode | 0x80, 0x00];
            self.peer.send_avc_passthrough(payload_2).await
        }
    }

    /// Sends SetAbsoluteVolume command to the peer.
    /// Returns the volume as reported by the peer.
    pub async fn set_absolute_volume(&self, volume: u8) -> Result<u8, Error> {
        let peer = self.peer.get_control_connection()?;
        let cmd = SetAbsoluteVolumeCommand::new(volume).map_err(|e| Error::PacketError(e))?;
        fx_vlog!(tag: "avrcp", 1, "set_absolute_volume send command {:#?}", cmd);
        let buf = RemotePeer::send_vendor_dependent_command(&peer, &cmd).await?;
        let response =
            SetAbsoluteVolumeResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        fx_vlog!(tag: "avrcp", 1, "set_absolute_volume received response {:#?}", response);
        Ok(response.volume())
    }

    /// Sends GetElementAttributes command to the peer.
    /// Returns all the media attributes received as a response or an error.
    pub async fn get_media_attributes(&self) -> Result<MediaAttributes, Error> {
        let peer = self.peer.get_control_connection()?;
        let mut media_attributes = MediaAttributes::new_empty();
        let cmd = GetElementAttributesCommand::all_attributes();
        fx_vlog!(tag: "avrcp", 1, "get_media_attributes send command {:#?}", cmd);
        let buf = RemotePeer::send_vendor_dependent_command(&peer, &cmd).await?;
        let response =
            GetElementAttributesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
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

    /// Send a GetCapabilities command requesting all supported events by the peer.
    /// Returns the supported NotificationEventIds by the peer or an error.
    pub async fn get_supported_events(&self) -> Result<Vec<NotificationEventId>, Error> {
        self.peer.get_supported_events().await
    }

    /// Send a GetPlayStatus command requesting current status of playing media.
    /// Returns the PlayStatus of current media on the peer, or an error.
    pub async fn get_play_status(&self) -> Result<PlayStatus, Error> {
        let peer = self.peer.get_control_connection()?;
        let cmd = GetPlayStatusCommand::new();
        fx_vlog!(tag: "avrcp", 1, "get_play_status send command {:?}", cmd);
        let buf = RemotePeer::send_vendor_dependent_command(&peer, &cmd).await?;
        let response =
            GetPlayStatusResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        fx_vlog!(tag: "avrcp", 1, "get_play_status received response {:?}", response);
        let mut play_status = PlayStatus::new_empty();
        play_status.song_length = if response.song_length != SONG_LENGTH_NOT_SUPPORTED {
            Some(response.song_length)
        } else {
            None
        };
        play_status.song_position = if response.song_position != SONG_POSITION_NOT_SUPPORTED {
            Some(response.song_position)
        } else {
            None
        };
        play_status.playback_status = Some(response.playback_status.into());
        Ok(play_status)
    }

    /// Sends a raw vendor dependent AVC command on the control channel. Returns the response
    /// from from the peer or an error. Used by the test controller and intended only for debugging.
    pub async fn send_raw_vendor_command<'a>(
        &'a self,
        pdu_id: u8,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, Error> {
        let command = RawVendorDependentPacket::new(PduId::try_from(pdu_id)?, payload);
        let peer = self.peer.get_control_connection()?;
        RemotePeer::send_vendor_dependent_command(&peer, &command).await
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
