// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use {log::trace, std::convert::TryInto};

use crate::packets::{SONG_LENGTH_NOT_SUPPORTED, SONG_POSITION_NOT_SUPPORTED};

#[derive(Debug, Clone)]
pub enum ControllerEvent {
    PlaybackStatusChanged(PlaybackStatus),
    TrackIdChanged(u64),
    PlaybackPosChanged(u32),
    VolumeChanged(u8),
}

pub type ControllerEventStream = mpsc::Receiver<ControllerEvent>;

/// Controller interface for a remote peer returned by the PeerManager using the
/// ControllerRequest stream for a given ControllerRequest.
#[derive(Debug)]
pub struct Controller {
    peer: RemotePeerHandle,
}

impl Controller {
    pub(crate) fn new(peer: RemotePeerHandle) -> Controller {
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
        let cmd = SetAbsoluteVolumeCommand::new(volume).map_err(|e| Error::PacketError(e))?;
        trace!("set_absolute_volume send command {:#?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response =
            SetAbsoluteVolumeResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        trace!("set_absolute_volume received response {:#?}", response);
        Ok(response.volume())
    }

    /// Sends GetElementAttributes command to the peer.
    /// Returns all the media attributes received as a response or an error.
    pub async fn get_media_attributes(&self) -> Result<MediaAttributes, Error> {
        let mut media_attributes = MediaAttributes::new_empty();
        let cmd = GetElementAttributesCommand::all_attributes();
        trace!("get_media_attributes send command {:#?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response =
            GetElementAttributesResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        trace!("get_media_attributes received response {:#?}", response);
        media_attributes.title = response.title;
        media_attributes.artist_name = response.artist_name;
        media_attributes.album_name = response.album_name;
        media_attributes.track_number = response.track_number;
        media_attributes.total_number_of_tracks = response.total_number_of_tracks;
        media_attributes.genre = response.genre;
        media_attributes.playing_time = response.playing_time;
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
        let cmd = GetPlayStatusCommand::new();
        trace!("get_play_status send command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response =
            GetPlayStatusResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        trace!("get_play_status received response {:?}", response);
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

    pub async fn get_current_player_application_settings(
        &self,
        attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
    ) -> Result<PlayerApplicationSettings, Error> {
        let cmd = GetCurrentPlayerApplicationSettingValueCommand::new(attribute_ids);
        trace!("get_current_player_application_settings command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = GetCurrentPlayerApplicationSettingValueResponse::decode(&buf[..])
            .map_err(|e| Error::PacketError(e))?;
        Ok(response.try_into()?)
    }

    pub async fn get_all_player_application_settings(
        &self,
    ) -> Result<PlayerApplicationSettings, Error> {
        // Get all the supported attributes.
        let cmd = ListPlayerApplicationSettingAttributesCommand::new();
        trace!("list_player_application_setting_attributes command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = ListPlayerApplicationSettingAttributesResponse::decode(&buf[..])
            .map_err(|e| Error::PacketError(e))?;

        // Get the text information of supported attributes.
        // TODO(fxbug.dev/41253): Get attribute text information for only custom attributes.
        let cmd = GetPlayerApplicationSettingAttributeTextCommand::new(
            response.player_application_setting_attribute_ids().clone(),
        );
        trace!("get_player_application_setting_attribute_text command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let _text_response = GetPlayerApplicationSettingAttributeTextResponse::decode(&buf[..])
            .map_err(|e| Error::PacketError(e))?;

        // For each attribute returned, get the set of possible values and text.
        for attribute in response.player_application_setting_attribute_ids() {
            let cmd = ListPlayerApplicationSettingValuesCommand::new(attribute);
            trace!("list_player_application_setting_values command {:?}", cmd);
            let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
            let list_response = ListPlayerApplicationSettingValuesResponse::decode(&buf[..])
                .map_err(|e| Error::PacketError(e))?;

            // TODO(fxbug.dev/41253): Get value text information for only custom attributes.
            let cmd = GetPlayerApplicationSettingValueTextCommand::new(
                attribute,
                list_response.player_application_setting_value_ids(),
            );
            trace!("get_player_application_setting_value_text command {:?}", cmd);
            let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
            let value_text_response =
                GetPlayerApplicationSettingValueTextResponse::decode(&buf[..])
                    .map_err(|e| Error::PacketError(e))?;
            trace!(
                "Response from get_player_application_setting_value_text: {:?}",
                value_text_response
            );
        }

        // TODO(fxbug.dev/41253): Use return value of ListPlayerApplicationSettingValuesResponse::decode()
        // to get current custom settings. For now, get current settings for default settings.
        self.get_current_player_application_settings(
            response.player_application_setting_attribute_ids().into(),
        )
        .await
    }

    pub async fn get_player_application_settings(
        &self,
        attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
    ) -> Result<PlayerApplicationSettings, Error> {
        if attribute_ids.is_empty() {
            self.get_all_player_application_settings().await
        } else {
            self.get_current_player_application_settings(attribute_ids).await
        }
    }

    pub async fn set_player_application_settings(
        &self,
        requested_settings: PlayerApplicationSettings,
    ) -> Result<PlayerApplicationSettings, Error> {
        let settings_vec = settings_to_vec(&requested_settings);

        // Default the returned `set_settings` to be the input `requested_settings`.
        let mut set_settings = requested_settings.clone();

        // If the command fails, the target did not accept the setting. Reflect
        // this in the returned `set_settings`.
        for setting in settings_vec {
            let cmd = SetPlayerApplicationSettingValueCommand::new(vec![setting]);
            trace!("set_player_application_settings command {:?}", cmd);
            let response_buf = self.peer.send_vendor_dependent_command(&cmd).await;

            match response_buf {
                Ok(buf) => {
                    let _ = SetPlayerApplicationSettingValueResponse::decode(&buf[..])
                        .map_err(|e| Error::PacketError(e))?;
                }
                Err(_) => {
                    let attribute = setting.0;
                    set_settings.clear_attribute(attribute);
                }
            }
        }
        Ok(set_settings)
    }

    /// Sends a raw vendor dependent AVC command on the control channel. Returns the response
    /// from from the peer or an error. Used by the test controller and intended only for debugging.
    pub async fn send_raw_vendor_command<'a>(
        &'a self,
        pdu_id: u8,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, Error> {
        let command = RawVendorDependentPacket::new(PduId::try_from(pdu_id)?, payload);
        self.peer.send_vendor_dependent_command(&command).await
    }

    /// For the FIDL test controller. Informational only and intended for logging only. The state is
    /// inherently racey.
    pub fn is_connected(&self) -> bool {
        self.peer.is_connected()
    }

    /// Returns notification events from the peer.
    pub fn take_event_stream(&self) -> ControllerEventStream {
        // TODO(fxbug.dev/44330) handle back pressure correctly and reduce mpsc::channel buffer sizes.
        let (sender, receiver) = mpsc::channel(512);
        self.peer.add_control_listener(sender);
        receiver
    }
}
