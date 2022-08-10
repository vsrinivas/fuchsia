// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_avrcp as fidl_avrcp,
    futures::channel::mpsc,
    packet_encoding::Decodable,
    parking_lot::Mutex,
    std::convert::{TryFrom, TryInto},
    std::num::NonZeroU16,
    tracing::trace,
};

use crate::packets::*;
use crate::peer::*;
use crate::types::PeerError as Error;

#[derive(Debug, Clone)]
pub enum ControllerEvent {
    PlaybackStatusChanged(PlaybackStatus),
    TrackIdChanged(u64),
    PlaybackPosChanged(u32),
    VolumeChanged(u8),
}

pub type ControllerEventStream = mpsc::Receiver<ControllerEvent>;

impl From<Error> for fidl_avrcp::BrowseControllerError {
    fn from(e: Error) -> Self {
        match e {
            Error::PacketError(_) => fidl_avrcp::BrowseControllerError::PacketEncoding,
            Error::AvctpError(_) => fidl_avrcp::BrowseControllerError::ProtocolError,
            Error::RemoteNotFound => fidl_avrcp::BrowseControllerError::RemoteNotConnected,
            Error::CommandNotSupported => fidl_avrcp::BrowseControllerError::CommandNotImplemented,
            Error::ConnectionFailure(_) => fidl_avrcp::BrowseControllerError::ConnectionError,
            _ => fidl_avrcp::BrowseControllerError::UnknownFailure,
        }
    }
}

fn into_media_element_item(
    x: fidl_avrcp::FileSystemItem,
) -> Result<fidl_avrcp::MediaElementItem, fidl_avrcp::BrowseControllerError> {
    match x {
        fidl_avrcp::FileSystemItem::MediaElement(m) => Ok(m),
        _ => Err(fidl_avrcp::BrowseControllerError::PacketEncoding),
    }
}

/// Controller interface for a remote peer returned by the PeerManager using the
/// ControllerRequest stream for a given ControllerRequest.
#[derive(Debug)]
pub struct Controller {
    peer: RemotePeerHandle,

    // Information about the browsable player that is currently set, if any.
    browsable_player: Mutex<Option<BrowsablePlayer>>,
}

/// Controller interface for a remote peer returned by the PeerManager using the
/// ControllerRequest stream for a given ControllerRequest.
#[allow(dead_code)]
#[derive(Debug)]
struct BrowsablePlayer {
    player_id: u16,
    // If the browsable player is not database aware, uid_counter will be none.
    uid_counter: Option<NonZeroU16>,
    // Number of items in the current folder.
    num_items: u32,
    sub_folders: Vec<String>,
}

impl BrowsablePlayer {
    fn new(player_id: u16, params: SetBrowsedPlayerResponseParams) -> Self {
        BrowsablePlayer {
            player_id,
            uid_counter: params.uid_counter().try_into().ok(),
            num_items: params.num_items(),
            sub_folders: params.folder_names(),
        }
    }

    fn uid_counter(&self) -> u16 {
        self.uid_counter.map_or(0, Into::into)
    }
}

impl Controller {
    pub(crate) fn new(peer: RemotePeerHandle) -> Controller {
        Controller { peer, browsable_player: Mutex::new(None) }
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
        let cmd = SetAbsoluteVolumeCommand::new(volume)?;
        trace!("set_absolute_volume send command {:#?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = SetAbsoluteVolumeResponse::decode(&buf[..])?;
        trace!("set_absolute_volume received response {:#?}", response);
        Ok(response.volume())
    }

    /// Sends GetElementAttributes command to the peer.
    /// Returns all the media attributes received as a response or an error.
    pub async fn get_media_attributes(&self) -> Result<fidl_avrcp::MediaAttributes, Error> {
        let mut media_attributes = fidl_avrcp::MediaAttributes::EMPTY;
        let cmd = GetElementAttributesCommand::all_attributes();
        trace!("get_media_attributes send command {:#?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = GetElementAttributesResponse::decode(&buf[..])?;
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
    pub async fn get_supported_events(&self) -> Result<HashSet<NotificationEventId>, Error> {
        self.peer.get_supported_events().await
    }

    /// Send a GetPlayStatus command requesting current status of playing media.
    /// Returns the PlayStatus of current media on the peer, or an error.
    pub async fn get_play_status(&self) -> Result<fidl_avrcp::PlayStatus, Error> {
        let cmd = GetPlayStatusCommand::new();
        trace!("get_play_status send command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = GetPlayStatusResponse::decode(&buf[..])?;
        trace!("get_play_status received response {:?}", response);
        let mut play_status = fidl_avrcp::PlayStatus::EMPTY;
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
        let response = GetCurrentPlayerApplicationSettingValueResponse::decode(&buf[..])?;
        Ok(response.try_into()?)
    }

    pub async fn get_all_player_application_settings(
        &self,
    ) -> Result<PlayerApplicationSettings, Error> {
        // Get all the supported attributes.
        let cmd = ListPlayerApplicationSettingAttributesCommand::new();
        trace!("list_player_application_setting_attributes command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = ListPlayerApplicationSettingAttributesResponse::decode(&buf[..])?;

        // Get the text information of supported attributes.
        // TODO(fxbug.dev/41253): Get attribute text information for only custom attributes.
        let cmd = GetPlayerApplicationSettingAttributeTextCommand::new(
            response.player_application_setting_attribute_ids().clone(),
        );
        trace!("get_player_application_setting_attribute_text command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let _text_response = GetPlayerApplicationSettingAttributeTextResponse::decode(&buf[..])?;

        // For each attribute returned, get the set of possible values and text.
        for attribute in response.player_application_setting_attribute_ids() {
            let cmd = ListPlayerApplicationSettingValuesCommand::new(attribute);
            trace!("list_player_application_setting_values command {:?}", cmd);
            let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
            let list_response = ListPlayerApplicationSettingValuesResponse::decode(&buf[..])?;

            // TODO(fxbug.dev/41253): Get value text information for only custom attributes.
            let cmd = GetPlayerApplicationSettingValueTextCommand::new(
                attribute,
                list_response.player_application_setting_value_ids(),
            );
            trace!("get_player_application_setting_value_text command {:?}", cmd);
            let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
            let value_text_response =
                GetPlayerApplicationSettingValueTextResponse::decode(&buf[..])?;
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
                    let _ = SetPlayerApplicationSettingValueResponse::decode(&buf[..])?;
                }
                Err(_) => {
                    let attribute = setting.0;
                    set_settings.clear_attribute(attribute);
                }
            }
        }
        Ok(set_settings)
    }

    pub async fn inform_battery_status(
        &self,
        battery_status: fidl_avrcp::BatteryStatus,
    ) -> Result<(), Error> {
        let cmd = InformBatteryStatusOfCtCommand::new(battery_status);
        trace!("inform_battery_status_of_ct command {:?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = InformBatteryStatusOfCtResponse::decode(&buf[..])?;
        trace!("inform_battery_status_of_ct received response {:?}", response);
        Ok(())
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

    pub async fn set_browsed_player(
        &self,
        player_id: u16,
    ) -> Result<(), fidl_avrcp::BrowseControllerError> {
        let cmd = SetBrowsedPlayerCommand::new(player_id);
        let buf = self.send_browse_command(PduId::SetBrowsedPlayer, &cmd).await?;
        let response = SetBrowsedPlayerResponse::decode(&buf[..]).map_err(Error::PacketError)?;
        let mut bp = self.browsable_player.lock();
        match response {
            SetBrowsedPlayerResponse::Failure(status) => {
                // Clear any previously-set browsed player.
                warn!("Failed to set a new browsable player (id = {}), keeping the previous browsable player ({:?})...", player_id, *bp);
                Err(status.into())
            }
            SetBrowsedPlayerResponse::Success(r) => {
                *bp = Some(BrowsablePlayer::new(player_id, r));
                info!("Browsable player changed (id = {})", player_id);
                Ok(())
            }
        }
    }

    fn check_browsed_player(&self) -> Result<(), fidl_avrcp::BrowseControllerError> {
        // AVRCP 1.6.2 Section 6.9.3 states that `SetBrowsedPlayer` command
        // shall be sent successfully before any other commands are sent on the
        // browsing channel except GetFolderItems in the Media Player List
        // scope.
        if self.browsable_player.lock().is_none() {
            return Err(fidl_avrcp::BrowseControllerError::NoAvailablePlayers);
        }
        Ok(())
    }

    pub async fn get_folder_items(
        &self,
        cmd: &GetFolderItemsCommand,
    ) -> Result<GetFolderItemsResponseParams, fidl_avrcp::BrowseControllerError> {
        let buf = self.send_browse_command(PduId::GetFolderItems, cmd).await?;
        let response = GetFolderItemsResponse::decode(&buf[..])?;
        trace!("get_folder_items received response {:?}", response);
        match response {
            GetFolderItemsResponse::Failure(status) => Err(status.into()),
            GetFolderItemsResponse::Success(r) => Ok(r),
        }
    }

    pub async fn get_file_system_items(
        &self,
        start_index: u32,
        end_index: u32,
        attribute_option: fidl_avrcp::AttributeRequestOption,
    ) -> Result<Vec<fidl_avrcp::FileSystemItem>, fidl_avrcp::BrowseControllerError> {
        let _ = self.check_browsed_player()?;

        let cmd = GetFolderItemsCommand::new_virtual_file_system(
            start_index,
            end_index,
            attribute_option,
        );
        let response = self.get_folder_items(&cmd).await?;
        response.item_list().into_iter().map(TryInto::try_into).collect()
    }

    pub async fn get_media_player_items(
        &self,
        start_index: u32,
        end_index: u32,
    ) -> Result<Vec<fidl_avrcp::MediaPlayerItem>, fidl_avrcp::BrowseControllerError> {
        // AVRCP 1.6.2 Section 6.9.3 states that GetFolderItems in the Media Player List scope command can be sent before
        // SetBrowsedPlayer command is sent over the browse channel.
        let cmd = GetFolderItemsCommand::new_media_player_list(start_index, end_index);
        let response = self.get_folder_items(&cmd).await?;
        response.item_list().into_iter().map(TryInto::try_into).collect()
    }

    pub async fn get_now_playing_items(
        &self,
        start_index: u32,
        end_index: u32,
        attribute_option: fidl_avrcp::AttributeRequestOption,
    ) -> Result<Vec<fidl_avrcp::MediaElementItem>, fidl_avrcp::BrowseControllerError> {
        let _ = self.check_browsed_player()?;

        let cmd =
            GetFolderItemsCommand::new_now_playing_list(start_index, end_index, attribute_option);
        let response = self.get_folder_items(&cmd).await?;
        response
            .item_list()
            .into_iter()
            .map(|item| item.try_into().and_then(into_media_element_item))
            .collect()
    }

    /// Changes directory from the current directory.
    /// If folder_uid is set to some value, it will invoke a ChangePath
    /// command with direction set to Folder Down.
    /// Otherwise, it will invoke a ChangePath command with direction set to
    /// Folder Up.
    /// If the command was successful, returns the number of items in the
    /// changed directory. Otherwise, error is returned.
    pub async fn change_directory(
        &self,
        folder_uid: Option<u64>,
    ) -> Result<u32, fidl_avrcp::BrowseControllerError> {
        let _ = self.check_browsed_player()?;

        let cmd = {
            let player = self.browsable_player.lock();
            ChangePathCommand::new(player.as_ref().unwrap().uid_counter(), folder_uid)
                .map_err(|_| fidl_avrcp::BrowseControllerError::PacketEncoding)?
        };
        let buf = self.send_browse_command(PduId::ChangePath, &cmd).await?;
        let response = ChangePathResponse::decode(&buf[..])?;
        trace!("change_directory received response {:?}", response);
        match response {
            ChangePathResponse::Failure(status) => Err(status.into()),
            ChangePathResponse::Success { num_of_items } => Ok(num_of_items),
        }
    }

    async fn send_browse_command(
        &self,
        pdu_id: PduId,
        command: &impl Encodable<Error = PacketError>,
    ) -> Result<Vec<u8>, Error> {
        let mut buf = vec![0; command.encoded_len()];
        let _ = command.encode(&mut buf[..])?;
        self.send_raw_browse_command(u8::from(&pdu_id), &buf).await
    }

    pub fn send_raw_browse_command(
        &self,
        pdu_id: u8,
        payload: &[u8],
    ) -> impl Future<Output = Result<Vec<u8>, Error>> {
        let cmd = BrowsePreamble::new(pdu_id, payload.to_vec());
        self.peer.send_browsing_command(pdu_id, cmd)
    }

    /// For the FIDL test controller. Informational only and intended for logging only. The state is
    /// inherently racey.
    pub fn is_control_connected(&self) -> bool {
        self.peer.is_control_connected()
    }

    /// For the FIDL test browse controller. Informational only and intended for logging only. The state is
    /// inherently racey.
    pub fn is_browse_connected(&self) -> bool {
        self.peer.is_browse_connected()
    }

    /// Creates new stream for events and returns the receiveing end for
    /// getting notification events from the peer.
    pub fn add_event_listener(&self) -> ControllerEventStream {
        // TODO(fxbug.dev/44330) handle back pressure correctly and reduce mpsc::channel buffer sizes.
        let (sender, receiver) = mpsc::channel(512);
        self.peer.add_control_listener(sender);
        receiver
    }
}
