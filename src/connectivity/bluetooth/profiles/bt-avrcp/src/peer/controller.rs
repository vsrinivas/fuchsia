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
    AvailablePlayersChanged,
    AddressedPlayerChanged(u16),
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
        let cmd = GetElementAttributesCommand::all_attributes();
        trace!("get_media_attributes send command {:#?}", cmd);
        let buf = self.peer.send_vendor_dependent_command(&cmd).await?;
        let response = GetElementAttributesResponse::decode(&buf[..])?;
        trace!("get_media_attributes received response {:#?}", response);
        Ok(fidl_avrcp::MediaAttributes {
            title: response.title,
            artist_name: response.artist_name,
            album_name: response.album_name,
            track_number: response.track_number,
            total_number_of_tracks: response.total_number_of_tracks,
            genre: response.genre,
            playing_time: response.playing_time,
            ..fidl_avrcp::MediaAttributes::EMPTY
        })
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
                    set_settings.clear_attribute(setting.0);
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

    pub async fn play_item(
        &self,
        uid: u64,
        scope: Scope,
    ) -> Result<(), fidl_avrcp::BrowseControllerError> {
        let _ = self.check_browsed_player()?;

        let cmd = {
            let player = self.browsable_player.lock();
            let p = player.as_ref().unwrap();
            PlayItemCommand::new(scope, uid, p.uid_counter())
        }?;

        let buf = self
            .peer
            .send_vendor_dependent_command(&cmd)
            .await
            .map_err(Into::<fidl_avrcp::BrowseControllerError>::into)?;
        let response = PlayItemResponse::decode(&buf[..]).map_err(|e| Error::PacketError(e))?;
        trace!("play_item received response {:?}", response);

        match response.status() {
            StatusCode::Success => Ok(()),
            e => Err(e.into()),
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

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use crate::peer_manager::TargetDelegate;
    use crate::profile::{AvrcpProtocolVersion, AvrcpService, AvrcpTargetFeatures};
    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use bt_avctp::{AvcPeer, AvctpCommand, AvctpCommandStream, AvctpPeer};
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::AttributeRequestOption;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::PeerId;
    use futures::TryStreamExt;

    const PLAYER_ID: u16 = 1004;
    const UID_COUNTER: u16 = 1;

    fn get_next_avctp_command(
        exec: &mut fasync::TestExecutor,
        command_stream: &mut AvctpCommandStream,
    ) -> AvctpCommand {
        exec.run_until_stalled(&mut command_stream.try_next())
            .expect("should be ready")
            .unwrap()
            .expect("has valid command")
    }

    /// Helper function for decoding AVCTP command. It checks that the PduId of the message is
    /// equal to the expected PduId.
    #[track_caller]
    fn decode_avctp_command(command: &AvctpCommand, expected_pdu_id: PduId) -> Vec<u8> {
        // Decode the provided `command` into a PduId and command parameters.
        match BrowseChannelHandler::decode_command(command) {
            Ok((id, packet)) if id == expected_pdu_id => packet,
            result => panic!("[Browse Channel] Received unexpected result: {:?}", result),
        }
    }

    /// Helper function to send AVCTP response back as a remote peer.
    fn send_avctp_response(
        pdu_id: PduId,
        response: &impl Encodable<Error = PacketError>,
        command: &AvctpCommand,
    ) {
        let mut buf = vec![0; response.encoded_len()];
        response.encode(&mut buf[..]).expect("should have succeeded");

        // Send the response back to the remote peer.
        let response_packet = BrowsePreamble::new(u8::from(&pdu_id), buf);
        let mut response_buf = vec![0; response_packet.encoded_len()];
        response_packet.encode(&mut response_buf[..]).expect("Encoding should work");

        let _ = command.send_response(&response_buf[..]).expect("should have succeeded");
    }

    fn set_browsed_player(
        exec: &mut fasync::TestExecutor,
        controller: &Controller,
        avctp_cmd_stream: &mut AvctpCommandStream,
    ) {
        let set_browsed_player_fut = controller.set_browsed_player(PLAYER_ID);
        pin_mut!(set_browsed_player_fut);

        exec.run_until_stalled(&mut set_browsed_player_fut).expect_pending("should not be ready");
        let command = get_next_avctp_command(exec, avctp_cmd_stream);
        // Ensure command params are correct.
        let params = decode_avctp_command(&command, PduId::SetBrowsedPlayer);
        let cmd =
            SetBrowsedPlayerCommand::decode(&params).expect("should have received valid command");
        assert_eq!(cmd.player_id(), PLAYER_ID);
        // Create mock response.
        let resp =
            SetBrowsedPlayerResponse::new_success(UID_COUNTER, 1, vec!["folder1".to_string()])
                .expect("should be initialized");
        send_avctp_response(PduId::SetBrowsedPlayer, &resp, &command);

        assert_matches!(
            exec.run_until_stalled(&mut set_browsed_player_fut).expect("should be ready"),
            Ok(())
        );
    }

    fn set_up() -> (Controller, AvcPeer, AvctpPeer) {
        let (profile_proxy, mut _profile_requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("should have initialized");
        let peer = RemotePeerHandle::spawn_peer(
            PeerId(0x1),
            Arc::new(TargetDelegate::new()),
            profile_proxy,
        );
        peer.set_target_descriptor(AvrcpService::Target {
            features: AvrcpTargetFeatures::CATEGORY1 | AvrcpTargetFeatures::SUPPORTSBROWSING,
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        });

        let (local_avc, remote_avc) = Channel::create();
        let (local_avctp, remote_avctp) = Channel::create();

        let remote_avc_peer = AvcPeer::new(remote_avc);
        let remote_avctp_peer = AvctpPeer::new(remote_avctp);

        peer.set_control_connection(AvcPeer::new(local_avc));
        peer.set_browse_connection(AvctpPeer::new(local_avctp));

        let controller = Controller::new(peer);

        (controller, remote_avc_peer, remote_avctp_peer)
    }

    fn verify_avc_command(
        exec: &mut fuchsia_async::TestExecutor,
        command_stream: &mut bt_avctp::AvcCommandStream,
        expected_pdu_id: PduId,
    ) {
        // When the control channel is first connected we get this.
        assert_matches!(exec.run_until_stalled(&mut command_stream.try_next()).expect("should be ready"),
        Ok(Some(command)) => {
            assert!(command.is_vendor_dependent());
            let (pdu_id, _) = decode_avc_vendor_command(&command).expect("should have succeeded");
            assert_eq!(pdu_id, expected_pdu_id);
        });
    }

    #[fuchsia::test]
    fn test_get_folder_items() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        let (controller, _remote_avc_peer, remote_avctp_peer) = set_up();
        let mut avctp_cmd_stream = remote_avctp_peer.take_command_stream();

        // Test GetNowPlayingItems, which should fail since browsed player isn't set.
        let get_now_playing_fut = controller.get_now_playing_items(
            START_INDEX,
            END_INDEX,
            AttributeRequestOption::GetAll(true),
        );
        pin_mut!(get_now_playing_fut);
        assert_matches!(
            exec.run_until_stalled(&mut get_now_playing_fut).expect("should be done"),
            Err(_)
        );

        // Test GetMediaPlayerItems. Should succeed even if browsed player isn't set.
        const START_INDEX: u32 = 0;
        const END_INDEX: u32 = 5;
        let get_media_players_fut = controller.get_media_player_items(START_INDEX, END_INDEX);
        pin_mut!(get_media_players_fut);

        exec.run_until_stalled(&mut get_media_players_fut).expect_pending("result not ready");
        let command = get_next_avctp_command(&mut exec, &mut avctp_cmd_stream);
        // Ensure command params are correct.
        let params = decode_avctp_command(&command, PduId::GetFolderItems);
        let cmd =
            GetFolderItemsCommand::decode(&params).expect("should have received valid command");
        assert_matches!(cmd.scope(),
        Scope::MediaPlayerList => {
            assert_eq!(cmd.start_item(), START_INDEX);
            assert_eq!(cmd.end_item(), END_INDEX);
            assert!(cmd.attribute_list().is_none());
        });
        // Create mock response.
        let resp = GetFolderItemsResponse::new_success(1, vec![]);
        send_avctp_response(PduId::GetFolderItems, &resp, &command);

        assert_matches!(
            exec.run_until_stalled(&mut get_media_players_fut).expect("should be ready"),
            Ok(list) if list.is_empty());

        set_browsed_player(&mut exec, &controller, &mut avctp_cmd_stream);

        // Test GetFileSystemItems. SetBrowsedPlayer was already called.
        let get_file_system_fut = controller.get_file_system_items(
            START_INDEX,
            END_INDEX,
            AttributeRequestOption::GetAll(false),
        );
        pin_mut!(get_file_system_fut);

        exec.run_until_stalled(&mut get_file_system_fut).expect_pending("result not ready");
        let command = get_next_avctp_command(&mut exec, &mut avctp_cmd_stream);
        // Ensure command params are correct.
        let params = decode_avctp_command(&command, PduId::GetFolderItems);
        let cmd =
            GetFolderItemsCommand::decode(&params).expect("should have received valid command");
        assert_matches!(cmd.scope(),
        Scope::MediaPlayerVirtualFilesystem => {
            assert_eq!(cmd.start_item(), START_INDEX);
                        assert_eq!(cmd.end_item(), END_INDEX);
                        assert_eq!(cmd.attribute_list().unwrap().len(), 0);
        });
        // Create mock response.
        let resp = GetFolderItemsResponse::new_success(1, vec![]);
        send_avctp_response(PduId::GetFolderItems, &resp, &command);

        let _ = exec.run_until_stalled(&mut get_file_system_fut).expect("should be ready");
    }

    #[fuchsia::test]
    fn test_change_directory() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        let (controller, _remote_avc_peer, remote_avctp_peer) = set_up();
        let mut avctp_cmd_stream = remote_avctp_peer.take_command_stream();
        set_browsed_player(&mut exec, &controller, &mut avctp_cmd_stream);

        let move_into_dir_fut = controller.change_directory(Some(2));
        pin_mut!(move_into_dir_fut);

        exec.run_until_stalled(&mut move_into_dir_fut).expect_pending("result not ready");
        let command = get_next_avctp_command(&mut exec, &mut avctp_cmd_stream);
        // Ensure command params are correct.
        let params = decode_avctp_command(&command, PduId::ChangePath);
        let cmd = ChangePathCommand::decode(&params).expect("should have received valid command");
        assert_eq!(cmd.uid_counter(), UID_COUNTER);
        assert_eq!(cmd.folder_uid(), Some(&std::num::NonZeroU64::new(2).unwrap()));
        // Create mock response.
        let resp = ChangePathResponse::new_success(10);
        send_avctp_response(PduId::ChangePath, &resp, &command);

        assert_matches!(
        exec.run_until_stalled(&mut move_into_dir_fut).expect("result ready"),
        Ok(num_of_items) => {
            assert_eq!(num_of_items, 10);
        });
    }

    #[fuchsia::test]
    fn test_play_item() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        let (controller, remote_avc_peer, remote_avctp_peer) = set_up();
        let mut avctp_cmd_stream = remote_avctp_peer.take_command_stream();
        let mut avc_cmd_stream = remote_avc_peer.take_command_stream();
        set_browsed_player(&mut exec, &controller, &mut avctp_cmd_stream);
        verify_avc_command(&mut exec, &mut avc_cmd_stream, PduId::GetCapabilities);

        // Test PlayFileSystemItem.
        let play_fut = controller.play_item(1, Scope::MediaPlayerVirtualFilesystem);
        pin_mut!(play_fut);
        exec.run_until_stalled(&mut play_fut).expect_pending("result not ready");
        let command = exec
            .run_until_stalled(&mut avc_cmd_stream.try_next())
            .expect("should be ready")
            .unwrap()
            .expect("has valid command");
        // Ensure command params are correct.
        assert!(command.is_vendor_dependent());
        let (pdu_id, body) = decode_avc_vendor_command(&command).expect("should succeed");
        assert_eq!(pdu_id, PduId::PlayItem);
        let cmd = PlayItemCommand::decode(body).expect("can decode");
        assert_eq!(cmd.uid(), 1);
        assert_eq!(cmd.scope(), Scope::MediaPlayerVirtualFilesystem);
        assert_eq!(cmd.uid_counter(), UID_COUNTER);
        // Create mock response.
        let resp = PlayItemResponse::new(StatusCode::Success);
        let packet = resp.encode_packet().expect("unable to encode packets for event");
        let _ = command
            .send_response(AvcResponseType::Accepted, &packet[..])
            .expect("should have succeeded");

        // Test PlayNowPlayingItem failure.
        let play_fut = controller.play_item(1, Scope::NowPlaying);
        pin_mut!(play_fut);
        exec.run_until_stalled(&mut play_fut).expect_pending("result not ready");
        let command = exec
            .run_until_stalled(&mut avc_cmd_stream.try_next())
            .expect("should be ready")
            .unwrap()
            .expect("has valid command");
        // Ensure scope is correct (skip other param checks since they were
        // done in the above call).
        assert!(command.is_vendor_dependent());
        let (_, body) = decode_avc_vendor_command(&command).expect("should succeed");
        let cmd = PlayItemCommand::decode(body).expect("can decode");
        assert_eq!(cmd.scope(), Scope::NowPlaying);
        // Create mock response that results in failure.
        let resp = PlayItemResponse::new(StatusCode::InternalError);
        let packet = resp.encode_packet().expect("unable to encode packets for event");
        let _ = command
            .send_response(AvcResponseType::Rejected, &packet[..])
            .expect("should have succeeded");

        assert_matches!(exec.run_until_stalled(&mut play_fut).expect("should be ready"), Err(_));
    }
}
