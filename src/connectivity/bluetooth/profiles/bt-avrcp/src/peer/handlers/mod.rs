// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use {
    fidl_fuchsia_bluetooth_avrcp::{AddressedPlayerId, Notification, TargetPassthroughError},
    fuchsia_async::Time,
    fuchsia_zircon::Duration,
    futures::future::Either,
    log::{error, trace},
    parking_lot::Mutex,
    std::collections::hash_map::Entry::{Occupied, Vacant},
    std::collections::VecDeque,
};

pub mod browse_channel;

mod decoders;

use decoders::*;

// Abstraction to assist with unit testing with mocks.
trait IncomingTargetCommand: std::fmt::Debug {
    fn packet_type(&self) -> AvcPacketType;
    fn op_code(&self) -> &AvcOpCode;
    fn body(&self) -> &[u8];

    fn send_response(
        &self,
        response_type: AvcResponseType,
        body: &[u8],
    ) -> Result<(), bt_avctp::Error>;
}

impl IncomingTargetCommand for AvcCommand {
    fn packet_type(&self) -> AvcPacketType {
        AvcCommand::avc_header(self).packet_type()
    }

    fn op_code(&self) -> &AvcOpCode {
        AvcCommand::avc_header(self).op_code()
    }

    fn body(&self) -> &[u8] {
        AvcCommand::body(self)
    }

    fn send_response(
        &self,
        response_type: AvcResponseType,
        body: &[u8],
    ) -> Result<(), bt_avctp::Error> {
        self.send_response(response_type, body)
    }
}

/// Handles commands received from the peer, typically when we are acting in target role for A2DP
/// source and absolute volume support for A2DP sink. Maintains state such as continuations and
/// registered notifications by the peer.
#[derive(Debug)]
pub struct ControlChannelHandler {
    inner: Arc<ControlChannelHandlerInner>,
}

#[derive(Debug)]
struct ControlChannelHandlerInner {
    peer_id: PeerId,
    target_delegate: Arc<TargetDelegate>,

    // Remaining continuations are stored as list of packet buffs keyed off the PduId that was
    // requested
    continuations: Arc<Continuations>,
}

/// Stores remaining packet continuations for the target
#[derive(Debug)]
struct Continuations {
    /// Renaming packets given a pdu_id
    remaining_packets: Mutex<HashMap<PduId, VecDeque<Vec<u8>>>>,
}

impl Continuations {
    fn new() -> Self {
        Self { remaining_packets: Mutex::new(HashMap::new()) }
    }

    fn pop_packet(&self, pdu_id: &PduId) -> Option<Vec<u8>> {
        match self.remaining_packets.lock().entry(pdu_id.clone()) {
            Vacant(_) => None,
            Occupied(mut entry) => {
                let packet = entry.get_mut().pop_front();
                if entry.get().is_empty() {
                    entry.remove();
                }
                packet
            }
        }
    }

    // Caches continuation packets given a pdu id.
    // If `packets` is empty, the continuation is cleared.
    // If an existing continuation exists, it's replaced.
    fn insert_remaining_packets(&self, pdu_id: &PduId, packets: Vec<Vec<u8>>) {
        if packets.is_empty() {
            self.remaining_packets.lock().remove_entry(pdu_id);
        } else {
            self.remaining_packets.lock().insert(pdu_id.clone(), VecDeque::from(packets));
        }
    }

    // Removes remaining packets given a pdu_id
    fn clear_continuation(&self, pdu_id: &PduId) {
        self.remaining_packets.lock().remove_entry(pdu_id);
    }

    // remove all continuations
    fn clear_all(&self) {
        self.remaining_packets.lock().clear();
    }
}

impl ControlChannelHandler {
    pub fn new(peer_id: &PeerId, target_delegate: Arc<TargetDelegate>) -> Self {
        Self {
            inner: Arc::new(ControlChannelHandlerInner {
                peer_id: peer_id.clone(),
                target_delegate,
                continuations: Arc::new(Continuations::new()),
            }),
        }
    }

    // we don't want to make IncomingTargetCommand trait pub.
    fn handle_command_internal(
        &self,
        command: impl IncomingTargetCommand,
    ) -> impl Future<Output = Result<(), Error>> {
        trace!("handle_command {:#?}", command);
        let inner = self.inner.clone();

        async move {
            // Step 1. Decode the command
            let decoded_command = match Command::decode_command(
                command.body(),
                command.packet_type(),
                command.op_code(),
            ) {
                // Step 2. Handle any decoding errors. If we have a decoding error, send the correct
                // error response and early return.
                Err(decode_error) => return send_decode_error_response(decode_error, command),
                Ok(x) => x,
            };

            let delegate = inner.target_delegate.clone();
            let continuations = inner.continuations.clone();

            // Step 3. Handle our current message depending on the type.
            match decoded_command {
                Command::Passthrough { command: avc_cmd, pressed } => {
                    handle_passthrough_command(delegate, command, avc_cmd, pressed).await
                }
                Command::VendorSpecific(cmd) => match cmd {
                    VendorSpecificCommand::Notify(cmd) => {
                        handle_notify_command(delegate, command, cmd).await
                    }
                    VendorSpecificCommand::Status(cmd) => {
                        handle_status_command(delegate, continuations, command, cmd).await
                    }
                    VendorSpecificCommand::Control(cmd) => {
                        handle_control_command(delegate, continuations, command, cmd).await
                    }
                },
            }
        }
    }

    /// Process an incoming AVC command on the control channel.
    /// The returned future when polled, returns an error if the command handler encounters an error
    /// that is unexpected and can't not be handled. It's generally expected that the peer
    /// connection should be closed and the command handler be reset.
    pub fn handle_command(&self, command: AvcCommand) -> impl Future<Output = Result<(), Error>> {
        self.handle_command_internal(command)
    }

    /// Clears any continuations and state. Should be called after connection with the peer has
    /// closed.
    pub fn reset(&self) {
        self.inner.continuations.clear_all();
    }
}

fn send_decode_error_response(
    decode_error: DecodeError,
    command: impl IncomingTargetCommand,
) -> Result<(), Error> {
    trace!("target incoming command decode error {:?}", decode_error);
    match decode_error {
        DecodeError::PassthroughInvalidPanelKey => command
            .send_response(AvcResponseType::NotImplemented, &[])
            .map_err(|e| Error::AvctpError(e)),
        DecodeError::VendorInvalidPreamble(pdu_id, _error) => {
            send_avc_reject(&command, pdu_id, StatusCode::InvalidCommand)
        }
        DecodeError::VendorPduNotImplemented(pdu_id) => {
            send_avc_reject(&command, pdu_id, StatusCode::InvalidCommand)
        }
        DecodeError::VendorPacketTypeNotImplemented(_packet_type) => {
            // remote sent a vendor packet that was not a status, control, or notify type.
            // the spec doesn't cover how to handle this specific error.
            command
                .send_response(AvcResponseType::NotImplemented, &[])
                .map_err(|e| Error::AvctpError(e))
        }
        DecodeError::VendorPacketDecodeError(_cmd_type, pdu_id, error) => {
            let status_code = match error {
                PacketError::InvalidMessageLength => StatusCode::InvalidCommand,
                PacketError::InvalidParameter => StatusCode::InvalidParameter,
                PacketError::InvalidMessage => StatusCode::ParameterContentError,
                PacketError::OutOfRange => StatusCode::InvalidCommand,
                _ => StatusCode::InternalError,
            };
            send_avc_reject(&command, u8::from(&pdu_id), status_code)
        }
    }
}

async fn handle_passthrough_command<'a>(
    delegate: Arc<TargetDelegate>,
    command: impl IncomingTargetCommand,
    key: AvcPanelCommand,
    pressed: bool,
) -> Result<(), Error> {
    // Passthrough commands need to be handled in 100ms
    let timer = fasync::Timer::new(Time::after(Duration::from_millis(100))).fuse();
    pin_mut!(timer);

    // As per Table 9.27 in AV/C 4.0, send back the state_flag, operation_id, and operation_data.
    let buf: Vec<u8> = command.body().to_vec();

    let handle_cmd = async {
        match delegate.send_passthrough_command(key, pressed).await {
            Ok(()) => AvcResponseType::Accepted,
            Err(TargetPassthroughError::CommandRejected) => AvcResponseType::Rejected,
            Err(TargetPassthroughError::CommandNotImplemented) => AvcResponseType::NotImplemented,
        }
    }
    .fuse();
    pin_mut!(handle_cmd);

    match futures::future::select(timer, handle_cmd).await {
        Either::Left((_, _)) => {
            // timer fired. let the client know it was rejected.
            command
                .send_response(AvcResponseType::Rejected, &buf[..])
                .map_err(|e| Error::AvctpError(e))
        }
        Either::Right((return_type, _)) => {
            command.send_response(return_type, &buf[..]).map_err(|e| Error::AvctpError(e))
        }
    }
}

fn send_avc_reject(
    command: &impl IncomingTargetCommand,
    pdu: u8,
    status_code: StatusCode,
) -> Result<(), Error> {
    let reject_response = RejectResponse::new(pdu, status_code);
    let buf = reject_response.encode_packet().expect("unable to encode reject packet");

    command.send_response(AvcResponseType::Rejected, &buf[..]).map_err(|e| Error::AvctpError(e))
}

/// Parse a notification and return a response encoder impl
fn notification_response(
    notification: &Notification,
    notify_event_id: &NotificationEventId,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    // Unsupported events:
    // Probably never: TrackReachedEnd, TrackReachedStart,
    // TODO(fxbug.dev/47597): Battery/power notifications: BattStatusChanged, SystemStatusChanged,
    // TODO(fxbug.dev/2744): Browse channel notifcations: NowPlayingContentChanged, AvailablePlayersChanged,
    //             and UidsChanged

    Ok(match notify_event_id {
        &NotificationEventId::EventPlaybackStatusChanged => {
            Box::new(PlaybackStatusChangedNotificationResponse::new(
                notification.status.ok_or(StatusCode::InternalError)?.into(),
            ))
        }
        &NotificationEventId::EventTrackChanged => Box::new(TrackChangedNotificationResponse::new(
            notification.track_id.ok_or(StatusCode::InternalError)?,
        )),
        &NotificationEventId::EventAddressedPlayerChanged => {
            // uid_counter is zero until we implement a uid database
            Box::new(AddressedPlayerChangedNotificationResponse::new(
                notification.player_id.ok_or(StatusCode::InternalError)?,
                0,
            ))
        }
        &NotificationEventId::EventPlaybackPosChanged => {
            Box::new(PlaybackPosChangedNotificationResponse::new(
                notification.pos.ok_or(StatusCode::InternalError)?,
            ))
        }
        &NotificationEventId::EventVolumeChanged => {
            Box::new(VolumeChangedNotificationResponse::new(
                notification.volume.ok_or(StatusCode::InternalError)?,
            ))
        }
        &NotificationEventId::EventPlayerApplicationSettingChanged => Box::new(
            PlayerApplicationSettingChangedResponse::from(PlayerApplicationSettings::from(
                notification.application_settings.as_ref().ok_or(StatusCode::InternalError)?,
            )),
        ),
        _ => return Err(StatusCode::InvalidParameter),
    })
}

fn send_notification(
    command: &impl IncomingTargetCommand,
    notify_event_id: &NotificationEventId,
    notification: &Notification,
    success_response_type: AvcResponseType,
) -> Result<(), Error> {
    match notification_response(&notification, notify_event_id) {
        Ok(encoder) => match encoder.encode_packet() {
            Ok(packet) => command
                .send_response(success_response_type, &packet[..])
                .map_err(|e| Error::AvctpError(e)),
            Err(e) => {
                error!("unable to encode target response packet {:?}", e);
                send_avc_reject(
                    command,
                    PduId::RegisterNotification as u8,
                    StatusCode::InternalError,
                )
            }
        },
        Err(status_code) => {
            send_avc_reject(command, PduId::RegisterNotification as u8, status_code)
        }
    }
}

async fn handle_notify_command(
    delegate: Arc<TargetDelegate>,
    command: impl IncomingTargetCommand,
    notify_command: RegisterNotificationCommand,
) -> Result<(), Error> {
    let pdu_id = notify_command.raw_pdu_id();

    if pdu_id != PduId::RegisterNotification as u8 {
        return send_avc_reject(&command, pdu_id, StatusCode::InvalidCommand);
    }

    let notification_fut = delegate.send_get_notification(notify_command.event_id().into()).fuse();
    pin_mut!(notification_fut);

    let interim_timer = fasync::Timer::new(Time::after(Duration::from_millis(1000))).fuse();
    pin_mut!(interim_timer);

    let notification: Notification = futures::select! {
        _ = interim_timer => {
            error!("target handler timed out with interim response");
            return send_avc_reject(&command, pdu_id, StatusCode::InternalError);
        }
        result = notification_fut => {
           match result {
               Ok(notification) => notification,
               Err(target_error) => {
                    return send_avc_reject(&command, pdu_id, target_error.into());
               }
           }
        }
    };

    // send interim value
    send_notification(
        &command,
        notify_command.event_id(),
        &notification,
        AvcResponseType::Interim,
    )?;

    let notification = match delegate
        .send_watch_notification(
            notify_command.event_id().into(),
            notification,
            notify_command.playback_interval(),
        )
        .await
    {
        Ok(notification) => notification,
        Err(target_error) => {
            return send_avc_reject(&command, pdu_id, target_error.into());
        }
    };

    // send changed value
    send_notification(&command, notify_command.event_id(), &notification, AvcResponseType::Changed)
}

async fn handle_get_capabilities(
    cmd: GetCapabilitiesCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    trace!("Received GetCapabilities Command {:#?}", cmd);

    match cmd.capability_id() {
        GetCapabilitiesCapabilityId::CompanyId => {
            // We don't advertise we support any company specific commands outside the BT SIG
            // company ID specific commands.
            let response = GetCapabilitiesResponse::new_btsig_company();
            Ok(Box::new(response))
        }
        GetCapabilitiesCapabilityId::EventsId => {
            let events = target_delegate.get_supported_events().await;
            let event_ids: Vec<u8> = events.into_iter().map(|i| i.into_primitive()).collect();
            let response = GetCapabilitiesResponse::new_events(&event_ids[..]);
            Ok(Box::new(response))
        }
    }
}

async fn handle_get_play_status(
    _cmd: GetPlayStatusCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let play_status =
        target_delegate.send_get_play_status_command().await.map_err(|e| StatusCode::from(e))?;

    let response = GetPlayStatusResponse {
        song_length: play_status.song_length.unwrap_or(SONG_LENGTH_NOT_SUPPORTED),
        song_position: play_status.song_position.unwrap_or(SONG_POSITION_NOT_SUPPORTED),
        playback_status: play_status.playback_status.map_or(PlaybackStatus::Stopped, |s| s.into()),
    };

    Ok(Box::new(response))
}

async fn handle_get_element_attributes(
    cmd: GetElementAttributesCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let element_attributes = target_delegate
        .send_get_media_attributes_command()
        .await
        .map_err(|e| StatusCode::from(e))?;

    let mut response = GetElementAttributesResponse::default();
    for attribute in cmd.attributes() {
        match &attribute {
            MediaAttributeId::Title => {
                response.title = element_attributes.title.clone();
            }
            MediaAttributeId::ArtistName => {
                response.artist_name = element_attributes.artist_name.clone();
            }
            MediaAttributeId::AlbumName => {
                response.album_name = element_attributes.album_name.clone();
            }
            MediaAttributeId::TrackNumber => {
                response.track_number = element_attributes.track_number.clone();
            }
            MediaAttributeId::TotalNumberOfTracks => {
                response.total_number_of_tracks = element_attributes.total_number_of_tracks.clone();
            }
            MediaAttributeId::Genre => {
                response.genre = element_attributes.genre.clone();
            }
            MediaAttributeId::PlayingTime => {
                response.playing_time = element_attributes.playing_time.clone();
            }
            _ => {
                error!("Support for attribute {:?} not implemented.", attribute);
            }
        }
    }

    Ok(Box::new(response))
}

async fn handle_list_player_application_setting_attributes(
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let attributes = target_delegate
        .send_list_player_application_setting_attributes_command()
        .await
        .map_err(|e| StatusCode::from(e))?;

    let response = ListPlayerApplicationSettingAttributesResponse::new(
        attributes.len() as u8,
        attributes.into_iter().map(|a| a.into()).collect(),
    );
    Ok(Box::new(response))
}

async fn handle_list_player_application_setting_values(
    cmd: ListPlayerApplicationSettingValuesCommand,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    // Currently, media players only support RepeatStatusMode and ShuffleMode.
    let values: Vec<u8> = match cmd.player_application_setting_attribute_id {
        PlayerApplicationSettingAttributeId::RepeatStatusMode => {
            RepeatStatusMode::VARIANTS.to_vec().into_iter().map(|v| u8::from(&v)).collect()
        }
        PlayerApplicationSettingAttributeId::ShuffleMode => {
            ShuffleMode::VARIANTS.to_vec().into_iter().map(|v| u8::from(&v)).collect()
        }
        _ => {
            return Err(StatusCode::InvalidParameter);
        }
    };

    let response = ListPlayerApplicationSettingValuesResponse::new(values.len() as u8, values);

    Ok(Box::new(response))
}

async fn handle_get_current_player_application_setting_value(
    cmd: GetCurrentPlayerApplicationSettingValueCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let current_values: PlayerApplicationSettings = target_delegate
        .send_get_player_application_settings_command(
            cmd.attribute_ids.into_iter().map(|id| id.into()).collect(),
        )
        .await
        .map(|v| PlayerApplicationSettings::from(&v))
        .map_err(|e| StatusCode::from(e))?;

    let response: GetCurrentPlayerApplicationSettingValueResponse = current_values.into();

    Ok(Box::new(response))
}

async fn handle_get_player_application_setting_attribute_text(
    cmd: GetPlayerApplicationSettingAttributeTextCommand,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let mut attribute_text: Vec<AttributeInfo> = vec![];
    // Send static text descriptions of the supported attributes.
    // Descriptions taken from AVRCP 1.6, Section 27, Appendix F.
    for attribute_id in cmd.attribute_ids {
        let desc;
        match attribute_id {
            PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                desc = "Repeat Mode status".to_string().into_bytes();
            }
            PlayerApplicationSettingAttributeId::ShuffleMode => {
                desc = "Shuffle ON/OFF status".to_string().into_bytes();
            }
            _ => {
                return Err(StatusCode::InvalidParameter);
            }
        }
        attribute_text.push(AttributeInfo::new(
            attribute_id,
            CharsetId::Utf8,
            desc.len() as u8,
            desc,
        ));
    }
    let response = GetPlayerApplicationSettingAttributeTextResponse::new(attribute_text);

    Ok(Box::new(response))
}

async fn handle_get_player_application_setting_value_text(
    cmd: GetPlayerApplicationSettingValueTextCommand,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let value_infos = match cmd.attribute_id() {
        PlayerApplicationSettingAttributeId::RepeatStatusMode => {
            repeat_status_mode_to_value_info(cmd.value_ids())
                .map_err(|_| StatusCode::InvalidParameter)?
        }
        PlayerApplicationSettingAttributeId::ShuffleMode => {
            shuffle_mode_to_value_info(cmd.value_ids()).map_err(|_| StatusCode::InvalidParameter)?
        }
        _ => {
            return Err(StatusCode::InvalidParameter);
        }
    };
    let response = GetPlayerApplicationSettingValueTextResponse::new(value_infos);

    Ok(Box::new(response))
}

/// Sends status command response. Send's Implemented/Stable on response code on success.
fn send_status_response(
    continuations: Arc<Continuations>,
    command: impl IncomingTargetCommand,
    result: Result<Box<dyn PacketEncodable>, StatusCode>,
    pdu_id: PduId,
) -> Result<(), Error> {
    match result {
        Ok(encodable) => match encodable.encode_packets() {
            Ok(mut packets) => {
                let first_packet = packets.remove(0);
                continuations.insert_remaining_packets(&pdu_id, packets);
                command
                    .send_response(AvcResponseType::ImplementedStable, &first_packet[..])
                    .map_err(|e| Error::AvctpError(e))
            }
            Err(e) => {
                error!("Error trying to encode response packet. Sending internal_error rejection to peer {:?}", e);
                send_avc_reject(&command, u8::from(&pdu_id), StatusCode::InternalError)
            }
        },
        Err(status_code) => {
            error!(
                "Error trying to encode response packet. Sending rejection to peer {:?}",
                status_code
            );
            send_avc_reject(&command, u8::from(&pdu_id), status_code)
        }
    }
}

async fn handle_status_command(
    delegate: Arc<TargetDelegate>,
    continuations: Arc<Continuations>,
    command: impl IncomingTargetCommand,
    status_command: StatusCommand,
) -> Result<(), Error> {
    let pdu_id = status_command.pdu_id();

    let status_fut = async {
        match status_command {
            StatusCommand::GetCapabilities(cmd) => handle_get_capabilities(cmd, delegate).await,
            StatusCommand::ListPlayerApplicationSettingAttributes(_) => {
                handle_list_player_application_setting_attributes(delegate).await
            }
            StatusCommand::ListPlayerApplicationSettingValues(cmd) => {
                handle_list_player_application_setting_values(cmd).await
            }
            StatusCommand::GetCurrentPlayerApplicationSettingValue(cmd) => {
                handle_get_current_player_application_setting_value(cmd, delegate).await
            }
            StatusCommand::GetPlayerApplicationSettingAttributeText(cmd) => {
                handle_get_player_application_setting_attribute_text(cmd).await
            }
            StatusCommand::GetPlayerApplicationSettingValueText(cmd) => {
                handle_get_player_application_setting_value_text(cmd).await
            }
            StatusCommand::GetElementAttributes(cmd) => {
                handle_get_element_attributes(cmd, delegate).await
            }
            StatusCommand::GetPlayStatus(cmd) => handle_get_play_status(cmd, delegate).await,
        }
    };

    // status interim responses should be returned in 100ms.
    // status final responses should be returned in 1000ms.

    let status_fut = status_fut.fuse();
    pin_mut!(status_fut);

    let interim_timer = fasync::Timer::new(Time::after(Duration::from_millis(100))).fuse();
    pin_mut!(interim_timer);

    loop {
        futures::select! {
            _ = interim_timer => {
                if let Err(e) = command.send_response(AvcResponseType::Interim, &[]) {
                    return Err(Error::AvctpError(e));
                }
            }
            result = status_fut => {
                return send_status_response(continuations, command, result, pdu_id);
            }
        }
    }
}

/// Sends control command response. Send's Accepted on response code on success.
fn send_control_response(
    command: impl IncomingTargetCommand,
    result: Result<Box<dyn PacketEncodable>, StatusCode>,
    pdu_id: PduId,
) -> Result<(), Error> {
    let encodable = match result {
        Err(status_code) => {
            error!(
                "Error handling command for {:?} - responding {:?} to peer",
                pdu_id, status_code
            );
            return send_avc_reject(&command, u8::from(&pdu_id), status_code);
        }
        Ok(encodable) => encodable,
    };
    match encodable.encode_packet() {
        Ok(packet) => command
            .send_response(AvcResponseType::Accepted, &packet[..])
            .map_err(|e| Error::AvctpError(e)),
        Err(e) => {
            error!("Error encoding response - sending InternalError to peer: {:?}", e);
            send_avc_reject(&command, u8::from(&pdu_id), StatusCode::InternalError)
        }
    }
}

async fn handle_set_player_application_setting_value(
    cmd: SetPlayerApplicationSettingValueCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let requested_settings =
        PlayerApplicationSettings::try_from(cmd).map_err(|_| StatusCode::InvalidParameter)?;

    let set_settings = target_delegate
        .send_set_player_application_settings_command(requested_settings.into())
        .await?;

    // Log the resulting set_settings, as it is valid for it to be different than the `requested_settings`.
    trace!("Media set player application settings: {:?}", set_settings);

    let response = SetPlayerApplicationSettingValueResponse::new();
    Ok(Box::new(response))
}

async fn handle_set_absolute_volume(
    cmd: SetAbsoluteVolumeCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let set_volume = target_delegate.send_set_absolute_volume_command(cmd.volume()).await?;

    let response =
        SetAbsoluteVolumeResponse::new(set_volume).map_err(|_| StatusCode::InternalError)?;

    Ok(Box::new(response))
}

fn handle_request_continuing_response(
    continuations: Arc<Continuations>,
    command: impl IncomingTargetCommand,
    cmd: RequestContinuingResponseCommand,
) -> Result<(), Error> {
    let pdu_id = match PduId::try_from(cmd.pdu_id_response()) {
        Err(_) => return send_avc_reject(&command, cmd.raw_pdu_id(), StatusCode::InvalidParameter),
        Ok(x) => x,
    };
    return match continuations.pop_packet(&pdu_id) {
        Some(packet) => command
            .send_response(AvcResponseType::ImplementedStable, &packet[..])
            .map_err(|e| Error::AvctpError(e)),
        None => send_avc_reject(&command, cmd.raw_pdu_id(), StatusCode::InvalidParameter),
    };
}

fn handle_abort_continuing_response(
    continuations: Arc<Continuations>,
    command: impl IncomingTargetCommand,
    cmd: AbortContinuingResponseCommand,
) -> Result<(), Error> {
    let pdu_id = match PduId::try_from(cmd.pdu_id_response()) {
        Err(_) => return send_avc_reject(&command, cmd.raw_pdu_id(), StatusCode::InvalidParameter),
        Ok(x) => x,
    };
    continuations.clear_continuation(&pdu_id);
    let response = AbortContinuingResponseResponse::new();
    let packet = response.encode_packet().map_err(|e| Error::PacketError(e))?;
    return command
        .send_response(AvcResponseType::Accepted, &packet[..])
        .map_err(|e| Error::AvctpError(e));
}

async fn handle_set_addressed_player(
    cmd: SetAddressedPlayerCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    let player_id = AddressedPlayerId { id: cmd.player_id() };
    // If the target rejects the requested `player_id`, the StatusCode will be bubbled up
    // as a reject.
    let _ = target_delegate.send_set_addressed_player_command(player_id).await?;

    let response = SetAddressedPlayerResponse::new(StatusCode::Success);
    Ok(Box::new(response))
}

async fn handle_control_command(
    delegate: Arc<TargetDelegate>,
    continuations: Arc<Continuations>,
    command: impl IncomingTargetCommand,
    control_command: ControlCommand,
) -> Result<(), Error> {
    // Handle continuations.
    // continuations are handled immediately and not asynchronously
    let control_command = match control_command {
        ControlCommand::RequestContinuingResponse(cmd) => {
            return handle_request_continuing_response(continuations, command, cmd);
        }
        ControlCommand::AbortContinuingResponse(cmd) => {
            return handle_abort_continuing_response(continuations, command, cmd);
        }
        _ => control_command,
    };

    let pdu_id = control_command.pdu_id();

    let control_fut = async {
        match control_command {
            ControlCommand::SetPlayerApplicationSettingValue(cmd) => {
                handle_set_player_application_setting_value(cmd, delegate).await
            }
            ControlCommand::SetAbsoluteVolume(cmd) => {
                handle_set_absolute_volume(cmd, delegate).await
            }
            ControlCommand::SetAddressedPlayer(cmd) => {
                handle_set_addressed_player(cmd, delegate).await
            }
            _ => Err(StatusCode::InvalidParameter),
        }
    };

    // control interim responses should be returned in 100ms.
    // control final responses should be returned in 200ms.

    let control_fut = control_fut.fuse();
    pin_mut!(control_fut);

    let interim_timer = fasync::Timer::new(Time::after(Duration::from_millis(100))).fuse();
    pin_mut!(interim_timer);

    loop {
        futures::select! {
            _ = interim_timer => {
                if let Err(e) = command.send_response(AvcResponseType::Interim, &[]) {
                    return Err(Error::AvctpError(e));
                }
            }
            result = control_fut => {
                return send_control_response(command, result, pdu_id);
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::peer_manager::TargetDelegate;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{
        self as fidl_avrcp, AbsoluteVolumeHandlerMarker, AbsoluteVolumeHandlerProxy,
        AbsoluteVolumeHandlerRequest, TargetAvcError, TargetHandlerMarker, TargetHandlerProxy,
        TargetHandlerRequest,
    };

    #[derive(Debug)]
    struct MockAvcCommandResponse {
        send_called: bool,
        data: Option<Vec<u8>>,
    }

    #[derive(Debug)]
    struct MockAvcCommand {
        packet_type: AvcPacketType,
        op_code: AvcOpCode,
        body: Vec<u8>,
        expect_response_interim_first: bool,
        expect_response_type: Option<AvcResponseType>,
        expect_body: Option<Vec<u8>>,
        expect_send: bool,
        response: Arc<Mutex<MockAvcCommandResponse>>,
    }

    impl MockAvcCommand {
        fn new(packet_type: AvcPacketType, op_code: AvcOpCode, body: Vec<u8>) -> Self {
            Self {
                packet_type,
                op_code,
                body,
                expect_response_interim_first: false,
                expect_response_type: None,
                expect_body: None,
                expect_send: false,
                response: Arc::new(Mutex::new(MockAvcCommandResponse {
                    send_called: false,
                    data: None,
                })),
            }
        }

        fn expect_response_type(mut self, response: AvcResponseType) -> Self {
            self.expect_response_type = Some(response);
            self.expect_send = true;
            self
        }

        fn expect_body(mut self, body: Vec<u8>) -> Self {
            self.expect_body = Some(body);
            self.expect_send = true;
            self
        }

        fn expect_reject(self) -> Self {
            self.expect_response_type(AvcResponseType::Rejected)
        }

        fn expect_reject_with_status_code(self, pdu_id: u8, status_code: StatusCode) -> Self {
            let reject_response = RejectResponse::new(pdu_id, status_code);
            let buf = reject_response.encode_packet().expect("unable to encode reject packet");
            self.expect_reject().expect_body(buf)
        }

        fn expect_accept(self) -> Self {
            self.expect_response_type(AvcResponseType::Accepted)
        }

        fn expect_stable(self) -> Self {
            self.expect_response_type(AvcResponseType::ImplementedStable)
        }

        fn expect_changed(self) -> Self {
            self.expect_response_type(AvcResponseType::Changed)
        }

        fn expect_interim(mut self) -> Self {
            self.expect_response_interim_first = true;
            self
        }

        fn response(&self) -> Arc<Mutex<MockAvcCommandResponse>> {
            self.response.clone()
        }
    }

    impl IncomingTargetCommand for MockAvcCommand {
        fn packet_type(&self) -> AvcPacketType {
            self.packet_type.clone()
        }

        fn op_code(&self) -> &AvcOpCode {
            &self.op_code
        }

        fn body(&self) -> &[u8] {
            &self.body[..]
        }

        fn send_response(
            &self,
            response_type: AvcResponseType,
            body: &[u8],
        ) -> Result<(), bt_avctp::Error> {
            let mut response_guard = self.response.lock();
            if !response_guard.send_called && self.expect_response_interim_first {
                assert_eq!(&response_type, &AvcResponseType::Interim);
            } else if let Some(expect_response_type) = &self.expect_response_type {
                assert_eq!(&response_type, expect_response_type);
            }

            if let Some(expect_body) = &self.expect_body {
                assert_eq!(body, &expect_body[..]);
            }

            response_guard.send_called = true;
            response_guard.data = Some(body.to_vec());

            Ok(())
        }
    }

    impl Drop for MockAvcCommand {
        fn drop(&mut self) {
            let send_called = self.response.lock().send_called;
            if self.expect_send && !send_called {
                assert!(false, "AvcCommand::send_response not called");
            }
        }
    }

    /// Creates a simple target handler that responds with error and basic values for most commands.
    fn create_dummy_target_handler(stall_responses: bool) -> TargetHandlerProxy {
        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");

        fasync::Task::spawn(async move {
            while let Some(Ok(event)) = target_stream.next().await {
                if stall_responses {
                    fasync::Timer::new(Time::after(Duration::from_millis(2000000))).await;
                }

                let _result = match event {
                    TargetHandlerRequest::GetEventsSupported { responder } => {
                        responder.send(&mut Err(TargetAvcError::RejectedInternalError))
                    }
                    TargetHandlerRequest::GetMediaAttributes { responder } => {
                        responder.send(&mut Ok(MediaAttributes {
                            title: Some("Foo".to_string()),
                            artist_name: Some("Bar".to_string()),
                            ..MediaAttributes::new_empty()
                        }))
                    }
                    TargetHandlerRequest::GetPlayStatus { responder } => {
                        responder.send(&mut Err(TargetAvcError::RejectedInternalError))
                    }
                    TargetHandlerRequest::SendCommand { command, pressed: _, responder } => {
                        if command == AvcPanelCommand::Play {
                            responder.send(&mut Ok(()))
                        } else {
                            responder.send(&mut Err(TargetPassthroughError::CommandNotImplemented))
                        }
                    }
                    TargetHandlerRequest::ListPlayerApplicationSettingAttributes { responder } => {
                        responder.send(&mut Ok(vec![
                            fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer,
                        ]))
                    }
                    TargetHandlerRequest::GetPlayerApplicationSettings {
                        attribute_ids: _,
                        responder,
                    } => responder.send(&mut Ok(fidl_avrcp::PlayerApplicationSettings {
                        shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                        ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                    })),
                    TargetHandlerRequest::SetPlayerApplicationSettings {
                        requested_settings: _,
                        responder,
                    } => {
                        responder.send(&mut Ok(fidl_avrcp::PlayerApplicationSettings::new_empty()))
                    }
                    TargetHandlerRequest::GetNotification { event_id: _, responder } => responder
                        .send(&mut Ok(Notification {
                            status: Some(fidl_fuchsia_bluetooth_avrcp::PlaybackStatus::Playing),
                            ..Notification::empty()
                        })),
                    TargetHandlerRequest::WatchNotification {
                        event_id: _,
                        current: _,
                        pos_change_interval: _,
                        responder,
                    } => responder.send(&mut Ok(Notification {
                        status: Some(fidl_fuchsia_bluetooth_avrcp::PlaybackStatus::Stopped),
                        ..Notification::empty()
                    })),
                    TargetHandlerRequest::SetAddressedPlayer { responder, .. } => {
                        responder.send(&mut Ok(()))
                    }
                    TargetHandlerRequest::GetMediaPlayerItems { responder, .. } => {
                        responder.send(&mut Err(TargetAvcError::RejectedNoAvailablePlayers))
                    }
                };
            }
        })
        .detach();

        target_proxy
    }

    fn create_command_handler(
        target_proxy: Option<TargetHandlerProxy>,
        absolute_volume_proxy: Option<AbsoluteVolumeHandlerProxy>,
    ) -> ControlChannelHandler {
        let target_delegate = Arc::new(TargetDelegate::new());
        if let Some(target_proxy) = target_proxy {
            target_delegate.set_target_handler(target_proxy).expect("unable to set target proxy");
        }

        if let Some(absolute_volume_proxy) = absolute_volume_proxy {
            target_delegate
                .set_absolute_volume_handler(absolute_volume_proxy)
                .expect("unable to set absolute_volume proxy");
        }

        let cmd_handler = ControlChannelHandler::new(&PeerId(1), target_delegate);
        cmd_handler
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_get_element_attribute_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x20, // GetElementAttributes pdu id
            0x00, // single packet
            0x00, 0x11, // param len, 17 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // NOW_PLAYING identifier
            0x02, // 2 attributes
            0x00, 0x00, 0x00, 0x01, // Title
            0x00, 0x00, 0x00, 0x02, // ArtistName
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_stable();

        cmd_handler.handle_command_internal(command).await
    }

    #[test]
    fn test_continuation_pop_empty() {
        let continuations = Continuations::new();
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_pop_empty_after_insert_empty() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(&PduId::GetElementAttributes, vec![]);
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_pop_one() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(&PduId::GetElementAttributes, vec![vec![1 as u8]]);
        assert_eq!(continuations.pop_packet(&PduId::GetElementAttributes), Some(vec![1 as u8]));
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_clear() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(&PduId::GetElementAttributes, vec![vec![1 as u8]]);
        continuations.clear_continuation(&PduId::GetElementAttributes);
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_reset() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(&PduId::GetElementAttributes, vec![vec![1 as u8]]);
        continuations.clear_all();
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_pop_many() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(
            &PduId::GetElementAttributes,
            vec![vec![1 as u8], vec![1 as u8]],
        );
        assert_eq!(continuations.pop_packet(&PduId::GetElementAttributes), Some(vec![1 as u8]));
        assert_eq!(continuations.pop_packet(&PduId::GetElementAttributes), Some(vec![1 as u8]));
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    #[test]
    fn test_continuation_replace() {
        let continuations = Continuations::new();
        continuations.insert_remaining_packets(
            &PduId::GetElementAttributes,
            vec![vec![1 as u8], vec![1 as u8]],
        );
        continuations.insert_remaining_packets(&PduId::GetElementAttributes, vec![]);
        assert!(continuations.pop_packet(&PduId::GetElementAttributes).is_none());
    }

    /// Test continuations work
    /// 1. Crafts a get_element_attribute response that spans 3 AVC packets.
    /// 2. Fetches the first packet and validate it's the first packet and the length
    /// 3. Fetches the second and validate it's a middle packet and the length
    /// 4. Sends an abort for the rest of the continuation, dropping the third
    /// 5. Attempts to fetch the last packet after aborting, expecting an error to validate abort
    ///    works.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_continuations() -> Result<(), Error> {
        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");

        // spawn a target handler that responds with a large element response
        fasync::Task::spawn(async move {
            while let Some(Ok(event)) = target_stream.next().await {
                let _result = match event {
                    TargetHandlerRequest::GetMediaAttributes { responder } => {
                        responder.send(&mut Ok(MediaAttributes {
                            title: Some(String::from(
                                "Lorem ipsum dolor sit amet,\
                 consectetur adipiscing elit. Nunc eget elit cursus ipsum \
                 fermentum viverra id vitae lorem. Cras luctus elementum \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 metus vel volutpat. Vestibulum ante ipsum primis in \
                 faucibus orci luctus et ultrices posuere cubilia \
                 Curae; Praesent efficitur velit sed metus luctus",
                            )),
                            artist_name: Some(String::from(
                                "elit euismod. \
                 Sed ex mauris, convallis a augue ac, hendrerit \
                 blandit mauris. Integer porttitor velit et posuere pharetra. \
                 Nullam ultricies justo sit amet lorem laoreet, id porta elit \
                 gravida. Suspendisse sed lectus eu lacus efficitur finibus. \
                 Sed egestas pretium urna eu pellentesque. In fringilla nisl dolor, \
                 sit amet luctus purus sagittis et. Mauris diam turpis, luctus et pretium nec, \
                 aliquam sed odio. Nulla finibus, orci a lacinia sagittis,\
                 urna elit ultricies dolor, et condimentum magna mi vitae sapien. \
                 Suspendisse potenti. Vestibulum ante ipsum primis in faucibus orci \
                 luctus et ultrices posuere cubilia Curae",
                            )),
                            album_name: Some(String::from(
                                "Mauris in ante ultrices, vehicula lorem non, sagittis metus.\
                 Nam facilisis volutpat quam. Suspendisse sem ipsum, blandit ut faucibus vitae,\
                 facilisis quis massa. Aliquam sagittis, orci sed dignissim vulputate, odio neque \
                 tempor dui, vel feugiat metus massa id urna. Nam at risus sem.\
                 Duis commodo suscipit metus, at placerat elit suscipit eget. Suspendisse interdum \
                 id metus vitae porta. Ut cursus viverra imperdiet. Aliquam erat volutpat. \
                 Curabitur vehicula mauris nec ex sollicitudin rhoncus. Integer ipsum libero, \
                 porta id velit et, egestas facilisis tellus.",
                            )),
                            genre: Some(String::from(
                                "Mauris in ante ultrices, vehicula lorem non, sagittis metus.\
                 Nam facilisis volutpat quam. Suspendisse sem ipsum, blandit ut faucibus vitae,\
                 facilisis quis massa. Aliquam sagittis, orci sed dignissim vulputate, odio neque \
                 tempor dui, vel feugiat metus massa id urna. Nam at risus sem.\
                 Duis commodo suscipit metus, at placerat elit suscipit eget. Suspendisse interdum \
                 id metus vitae porta. Ut cursus viverra imperdiet. Aliquam erat volutpat. \
                 Curabitur vehicula mauris nec ex sollicitudin rhoncus. Integer ipsum libero, \
                 porta id velit et, egestas facilisis tellus.",
                            )),
                            ..MediaAttributes::new_empty()
                        }))
                    }
                    _ => {
                        assert!(false, "unexpected event");
                        Ok(())
                    }
                };
            }
        })
        .detach();

        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // send first packet
        {
            // GetElementAttributes
            let packet_body: Vec<u8> = [
                0x20, // GetElementAttributes pdu id
                0x00, // single packet
                0x00, 0x11, // param len, 17 bytes
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // NOW_PLAYING identifier
                0x02, // 4 attributes
                0x00, 0x00, 0x00, 0x01, // Title
                0x00, 0x00, 0x00, 0x02, // ArtistName
                0x00, 0x00, 0x00, 0x03, // AlbumName
                0x00, 0x00, 0x00, 0x06, // Genre
            ]
            .to_vec();

            let command = MockAvcCommand::new(
                AvcPacketType::Command(AvcCommandType::Status),
                AvcOpCode::VendorDependent,
                packet_body,
            )
            .expect_stable();

            let response = command.response();

            let _ = cmd_handler.handle_command_internal(command).await?;

            let response_guard = response.lock();
            let response_data = response_guard.data.as_ref().expect("data");
            assert_eq!(response_data.len(), 512); // The AVC packet should be 512
            assert_eq!(response_data[1], 0x01); // packet should be marked as the first packet in the preamble.
        }

        // send continuation packet
        {
            // RequestContinuingResponse
            let packet_body: Vec<u8> = [
                0x40, // RequestContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x01, // param len, 1 byte
                0x20, // GetElementAttributes pdu id
            ]
            .to_vec();

            let command = MockAvcCommand::new(
                AvcPacketType::Command(AvcCommandType::Control),
                AvcOpCode::VendorDependent,
                packet_body,
            )
            .expect_stable();

            let response = command.response();

            let _ = cmd_handler.handle_command_internal(command).await?;

            let response_guard = response.lock();
            let response_data = response_guard.data.as_ref().expect("data");
            assert_eq!(response_data.len(), 512); // The AVC packet should be 512
            assert_eq!(response_data[2], 0x01); // packet should be marked as a middle packet in the preamble.
        }

        // send abort packet
        {
            // AbortContinuingResponse
            let packet_body: Vec<u8> = [
                0x41, // AbortContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x01, // param len, 1 byte
                0x20, // GetElementAttributes pdu id
            ]
            .to_vec();

            let command = MockAvcCommand::new(
                AvcPacketType::Command(AvcCommandType::Control),
                AvcOpCode::VendorDependent,
                packet_body,
            )
            .expect_accept();

            let _ = cmd_handler.handle_command_internal(command).await?;
        }

        // send another continuation packet to test abort worked
        {
            // RequestContinuingResponse
            let packet_body: Vec<u8> = [
                0x40, // RequestContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x01, // param len, 17 bytes
                0x20, // GetElementAttributes pdu id
            ]
            .to_vec();

            let command = MockAvcCommand::new(
                AvcPacketType::Command(AvcCommandType::Control),
                AvcOpCode::VendorDependent,
                packet_body,
            )
            .expect_reject();

            let _ = cmd_handler.handle_command_internal(command).await?;
        }

        Ok(())
    }

    /// send get_capabilities
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_send_get_capabilities() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // get_capabilities
        let packet_body: Vec<u8> = [
            0x10, // get_capabilities pdu id
            0x00, // single packet
            0x00, 0x08, // param len, 8 bytes
            0x02, // company_id
            0x02, // len
            0x00, 0x19, 0x58, // BT_SIG Company ID
            0xff, 0xff, 0xff, // random
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_stable();

        cmd_handler.handle_command_internal(command).await
    }

    /// Validate our interim timers fire
    #[test]
    fn send_status_interim_fired() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");

        // stall the responses from the target handler by 10 seconds
        let target_proxy = create_dummy_target_handler(true);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // get_capabilities
        let packet_body: Vec<u8> = [
            0x10, // get_capabilities pdu id
            0x00, // single packet
            0x00, 0x08, // param len, 8 bytes
            0x03, // event_id
            0x01, // len
            0x01, // event_id
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_interim();

        let handle_cmd = cmd_handler.handle_command_internal(command);
        pin_utils::pin_mut!(handle_cmd);
        assert!(exec.run_until_stalled(&mut handle_cmd).is_pending());
        exec.wake_next_timer();
        // we should still be pending.
        assert!(exec.run_until_stalled(&mut handle_cmd).is_pending());
        // we drop the mock command and if the expected interim wasn't called, the test will fail
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_list_player_application_setting_attributes_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x11, // ListPlayerApplicationSettings pdu id
            0x00, // Single packet
            0x00, 0x00, // param_len, 0 bytes
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = [
            0x11, // ListPlayerApplicationSettings pdu id
            0x00, // Single packet
            0x00, 0x02, // param_len, 2 bytes
            0x01, // Number of settings, 1
            0x01, // Equalizer
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_body(expected_packet_response)
        .expect_response_type(AvcResponseType::ImplementedStable);

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_list_player_application_setting_values_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x12, // ListPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x01, // param_len, 1 byte
            0x02, // Attribute: RepeatStatusMode
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = [
            0x12, // ListPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x05, // param_len, 5 bytes
            0x04, // Number of settings, 4
            0x01, 0x02, 0x03, 0x04, // The 4 values RepeatStatusMode can take.
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_body(expected_packet_response)
        .expect_response_type(AvcResponseType::ImplementedStable);

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_get_player_application_settings_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x13, // GetCurrentPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x02, // param_len, 2 bytes
            0x01, // Number of settings: 1
            0x03, // Attribute: ShuffleMode
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = [
            0x13, // GetCurrentPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x03, // param_len: 3 bytes
            0x01, // Number of settings: 1
            0x03, // Attribute: ShuffleMode
            0x01, // Value: Off = 1
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_body(expected_packet_response)
        .expect_response_type(AvcResponseType::ImplementedStable);

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_get_player_application_setting_attribute_text_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x15, // GetPlayerApplicationSettingAttributeText pdu id
            0x00, // Single packet
            0x00, 0x02, // param_len, 2 bytes
            0x01, // Number of settings: 1
            0x03, // Attribute: Shuffle mode
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = [
            0x15, // GetPlayerApplicationSettingAttributeText pdu id
            0x00, // Single packet
            0x00, 0x1A, // param_len, 26 bytes
            0x01, // Number of settings, 1
            0x03, // Attribute: ShuffleMode
            0x00, 0x6a, // CharacterSet: Utf-8
            0x15, // Value length: 21
            0x53, 0x68, 0x75, 0x66, 0x66, 0x6c, 0x65, 0x20, // "Shuffle ON/OFF status"
            0x4f, 0x4e, 0x2f, 0x4f, 0x46, 0x46, 0x20, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73,
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_body(expected_packet_response)
        .expect_response_type(AvcResponseType::ImplementedStable);

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_get_player_application_setting_value_text_cmd() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x16, // GetPlayerApplicationSettingValueText pdu id
            0x00, // Single packet
            0x00, 0x04, // param_len, 4 bytes
            0x02, // Attribute: Repeat mode
            0x02, // Number of values: 2
            0x01, // Off
            0x02, // Single track
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = [
            0x16, // GetPlayerApplicationSettingValueText pdu id
            0x00, // Single packet
            0x00, 0x1f, // param_len,  bytes
            0x02, // Number of values, 2
            0x01, // Value: Off
            0x00, 0x6a, // CharacterSet: Utf-8
            0x03, // Value length: 3
            0x4f, 0x66, 0x66, // Off
            0x02, // Value: Single track repeat
            0x00, 0x6a, // CharacterSet: Utf-8
            0x13, // Value length: 19 bytes
            0x53, 0x69, 0x6e, 0x67, 0x6c, 0x65, 0x20, 0x74, 0x72, 0x61, 0x63, 0x6b, 0x20, 0x72,
            0x65, 0x70, 0x65, 0x61, 0x74, // "Single track repeat"
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Status),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_body(expected_packet_response)
        .expect_response_type(AvcResponseType::ImplementedStable);

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_set_player_application_setting_value() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x14, // SetPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x03, // param_len, 3 bytes
            0x01, // Number of settings: 1
            0x02, // Setting attribute: Repeat Status mode
            0x01, // Setting value: Off
        ]
        .to_vec();
        let _expected_packet_response: Vec<u8> = [
            0x14, // SetPlayerApplicationSettingValue pdu id
            0x00, // Single packet
            0x00, 0x01, // param_len,  bytes
            0x04,
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_accept();

        cmd_handler.handle_command_internal(command).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_set_addressed_player_command() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // generic vendor status command
        let packet_body: Vec<u8> = [
            0x60, // SetAddressedPlayer pdu id
            0x00, // Single packet
            0x00, 0x02, // param_len: 2 bytes
            0x00, 0x02, // player_id
        ]
        .to_vec();

        let expected_packet_response: Vec<u8> = [
            0x60, // SetAddressedPlayer pdu id
            0x00, // Single packet
            0x00, 0x01, // param_len, 1 byte
            0x04, // Response code, success
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_accept()
        .expect_body(expected_packet_response);

        cmd_handler.handle_command_internal(command).await
    }

    /// send passthrough is implemented. expect it's accepted
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_send_passthrough() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // play key
        let packet_body: Vec<u8> = [
            0x44, // play key. key down
            0x00, // additional params len is 0
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = packet_body.clone();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::Passthrough,
            packet_body,
        )
        .expect_accept()
        .expect_body(expected_packet_response);

        cmd_handler.handle_command_internal(command).await
    }

    /// Test correctness of response of a passthrough command that is not implemented.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_send_passthrough_not_implemented() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // play key
        let packet_body: Vec<u8> = [
            0x4b, // Forward key, pressed down. Not implemented by mock target handler.
            0x00, // additional params len is 0
        ]
        .to_vec();
        let expected_packet_response: Vec<u8> = packet_body.clone();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::Passthrough,
            packet_body,
        )
        .expect_response_type(AvcResponseType::NotImplemented)
        .expect_body(expected_packet_response);

        cmd_handler.handle_command_internal(command).await
    }

    #[test]
    fn validate_passthrough_reject_timer_fired() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");

        let target_proxy = create_dummy_target_handler(true);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        // play key
        let packet_body: Vec<u8> = [
            0x44, // play key. key down
            0x00, // additional params len is 0
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::Passthrough,
            packet_body,
        )
        .expect_reject();

        let handle_cmd = cmd_handler.handle_command_internal(command);
        pin_utils::pin_mut!(handle_cmd);
        assert!(exec.run_until_stalled(&mut handle_cmd).is_pending());
        exec.wake_next_timer();
        // we should be ready right away
        assert!(exec.run_until_stalled(&mut handle_cmd).is_ready());
        // we drop the mock command and if the expected reject wasn't called, the test will fail
    }

    /// test notifications on absolute volume handler
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_register_notification_volume() -> Result<(), Error> {
        let (volume_proxy, mut volume_stream) =
            create_proxy_and_stream::<AbsoluteVolumeHandlerMarker>()
                .expect("Error creating AbsoluteVolumeHandler endpoint");
        let cmd_handler = create_command_handler(None, Some(volume_proxy));

        fasync::Task::spawn(async move {
            while let Some(Ok(event)) = volume_stream.next().await {
                match event {
                    AbsoluteVolumeHandlerRequest::GetCurrentVolume { responder } => {
                        let _result = responder.send(10);
                    }
                    AbsoluteVolumeHandlerRequest::OnVolumeChanged { responder } => {
                        let _result = responder.send(11);
                    }
                    _ => panic!("unexpected command"),
                };
            }
        })
        .detach();

        let packet_body: Vec<u8> = [
            0x31, // RegisterNotification
            0x00, // single packet
            0x00, 0x05, // param len, 4 bytes
            0x0d, 0x00, 0x00, 0x00, 0x00, // volume change event
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Notify),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_interim()
        .expect_changed();

        cmd_handler.handle_command_internal(command).await
    }

    /// test notifications on target handler
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_register_notification_target() -> Result<(), Error> {
        let target_proxy = create_dummy_target_handler(false);
        let cmd_handler = create_command_handler(Some(target_proxy), None);

        let packet_body: Vec<u8> = [
            0x31, // RegisterNotification
            0x00, // single packet
            0x00, 0x05, // param len, 4 bytes
            0x01, 0x00, 0x00, 0x00, 0x00, // playback status change event
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Notify),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_interim()
        .expect_changed();

        cmd_handler.handle_command_internal(command).await
    }

    /// test we get a command and it responds as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_set_absolute_volume_cmd() -> Result<(), Error> {
        let (volume_proxy, mut volume_stream) =
            create_proxy_and_stream::<AbsoluteVolumeHandlerMarker>()
                .expect("Error creating AbsoluteVolumeHandler endpoint");

        let cmd_handler = create_command_handler(None, Some(volume_proxy));

        // vendor status command
        let packet_body: Vec<u8> = [
            0x50, // SetAbsoluteVolumeCommand
            0x00, // single packet
            0x00, 0x01, // param len, 1 byte
            0x20, // volume level
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_accept()
        .expect_body(vec![
            0x50, // SetAbsoluteVolumeCommand
            0x00, // single packet
            0x00, 0x01, // param len, 1 byte
            0x32, // volume level
        ]);

        let handle_command_fut = cmd_handler.handle_command_internal(command).fuse();
        pin_mut!(handle_command_fut);

        let handle_stream = async move {
            match volume_stream.next().await {
                Some(Ok(AbsoluteVolumeHandlerRequest::SetVolume {
                    requested_volume,
                    responder,
                })) => {
                    assert_eq!(requested_volume, 0x20); // 0x20 is the encoded volume
                    responder.send(0x32 as u8).expect("unable to send");
                }
                _ => assert!(false, "unexpected state"),
            }
        }
        .fuse();
        pin_mut!(handle_stream);

        loop {
            futures::select! {
                _= handle_stream => {},
                result = handle_command_fut => {
                    return result
                }
            }
        }
    }

    /// send a command with a badly formed packet and see if we get the reject error we are expecting.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_set_absolute_volume_cmd_bad_packet() -> Result<(), Error> {
        // absolute volume handler shouldn't even get called since the packet decode should fail.
        let (volume_proxy, volume_stream) =
            create_proxy_and_stream::<AbsoluteVolumeHandlerMarker>()
                .expect("Error creating AbsoluteVolumeHandler endpoint");

        let cmd_handler = create_command_handler(None, Some(volume_proxy));

        // encode invalid packet
        let packet_body: Vec<u8> = [
            0x50, // SetAbsoluteVolumeCommand
            0x00, // single packet
            0x00, 0x00, // param len, 0 byte
        ]
        .to_vec();

        let command = MockAvcCommand::new(
            AvcPacketType::Command(AvcCommandType::Control),
            AvcOpCode::VendorDependent,
            packet_body,
        )
        .expect_reject_with_status_code(0x50, StatusCode::ParameterContentError);

        cmd_handler.handle_command_internal(command).await?;

        drop(volume_stream);
        Ok(())
    }
}
