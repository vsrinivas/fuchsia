// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_bluetooth_avrcp::TargetPassthroughError;

mod decoders;

use decoders::*;

use fuchsia_async::Time;
use fuchsia_zircon::Duration;
use futures::future::Either;

use bt_avctp;

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
}

impl ControlChannelHandler {
    pub fn new(peer_id: &PeerId, target_delegate: Arc<TargetDelegate>) -> Self {
        Self {
            inner: Arc::new(ControlChannelHandlerInner {
                peer_id: peer_id.clone(),
                target_delegate,
            }),
        }
    }

    // we don't want to make IncomingTargetCommand trait pub.
    fn handle_command_internal(
        &self,
        command: impl IncomingTargetCommand,
    ) -> impl Future<Output = Result<(), Error>> {
        fx_vlog!(tag: "avrcp", 2, "handle_command {:#?}", command);
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
                        handle_status_command(delegate, command, cmd).await
                    }
                    VendorSpecificCommand::Control(cmd) => {
                        handle_control_command(delegate, command, cmd).await
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
    // TODO(41699): add continuations for get_element_attributes and wire up reset logic here
    pub fn reset(&self) {}
}

fn send_decode_error_response(
    decode_error: DecodeError,
    command: impl IncomingTargetCommand,
) -> Result<(), Error> {
    fx_vlog!(tag: "avrcp", 2, "target incoming command decode error {:?}", decode_error);
    match decode_error {
        DecodeError::PassthroughInvalidPanelKey => command
            .send_response(AvcResponseType::NotImplemented, &[])
            .map_err(|e| Error::AvctpError(e)),
        DecodeError::VendorInvalidPreamble(pdu_id, _error) => {
            send_avc_reject(command, pdu_id, StatusCode::InvalidCommand)
        }
        DecodeError::VendorPduNotImplemented(pdu_id) => {
            send_avc_reject(command, pdu_id, StatusCode::InvalidParameter)
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
            send_avc_reject(command, u8::from(&pdu_id), status_code)
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
            command.send_response(AvcResponseType::Rejected, &[]).map_err(|e| Error::AvctpError(e))
        }
        Either::Right((return_type, _)) => {
            command.send_response(return_type, &[]).map_err(|e| Error::AvctpError(e))
        }
    }
}

fn send_avc_reject(
    command: impl IncomingTargetCommand,
    pdu: u8,
    status_code: StatusCode,
) -> Result<(), Error> {
    let reject_response = RejectResponse::new(pdu, status_code);
    let buf = reject_response.encode_packet().expect("unable to encode reject packet");

    command.send_response(AvcResponseType::Rejected, &buf[..]).map_err(|e| Error::AvctpError(e))
}

// TODO(41701): implement target notifications.
async fn handle_notify_command(
    _delegate: Arc<TargetDelegate>,
    command: impl IncomingTargetCommand,
    _notify_command: RegisterNotificationCommand,
) -> Result<(), Error> {
    // TODO(41701): remove. temporary response
    command.send_response(AvcResponseType::NotImplemented, &[]).map_err(|e| Error::AvctpError(e))
}

async fn handle_get_capabilities(
    cmd: GetCapabilitiesCommand,
    target_delegate: Arc<TargetDelegate>,
) -> Result<Box<dyn PacketEncodable>, StatusCode> {
    fx_vlog!(tag: "avrcp", 2, "Received GetCapabilities Command {:#?}", cmd);

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
                fx_log_err!("Support for attribute {:?} not implemented.", attribute);
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

/// Sends status command response. Send's Implemented/Stable on response code on success.
fn send_status_response(
    command: impl IncomingTargetCommand,
    result: Result<Box<dyn PacketEncodable>, StatusCode>,
    pdu_id: PduId,
) -> Result<(), Error> {
    match result {
        Ok(encodable) => {
            match encodable.encode_packets() {
                Ok(mut packets) => {
                    let first_packet = packets.remove(0);
                    // TODO(41699): send the first packet and push the other packets in this list,
                    //   if any, into a continuation on the inner.
                    command
                        .send_response(AvcResponseType::ImplementedStable, &first_packet[..])
                        .map_err(|e| Error::AvctpError(e))
                }
                Err(e) => {
                    fx_log_err!("Error trying to encode response packet. Sending internal_error rejection to peer {:?}", e);
                    send_avc_reject(command, u8::from(&pdu_id), StatusCode::InternalError)
                }
            }
        }
        Err(status_code) => {
            fx_log_err!(
                "Error trying to encode response packet. Sending rejection to peer {:?}",
                status_code
            );
            send_avc_reject(command, u8::from(&pdu_id), status_code)
        }
    }
}

async fn handle_status_command(
    delegate: Arc<TargetDelegate>,
    command: impl IncomingTargetCommand,
    status_command: StatusCommand,
) -> Result<(), Error> {
    let pdu_id = status_command.pdu_id();

    let status_fut = async {
        match status_command {
            StatusCommand::GetCapabilities(cmd) => handle_get_capabilities(cmd, delegate).await,
            /* Todo: implement
            StatusCommand::ListPlayerApplicationSettingValues(_) => {}
            StatusCommand::GetCurrentPlayerApplicationSettingValue(_) => {}
            StatusCommand::GetPlayerApplicationSettingAttributeText(_) => {}
            StatusCommand::GetPlayerApplicationSettingValueText(_) => {}
            */
            StatusCommand::ListPlayerApplicationSettingAttributes(_) => {
                handle_list_player_application_setting_attributes(delegate).await
            }
            StatusCommand::GetElementAttributes(cmd) => {
                handle_get_element_attributes(cmd, delegate).await
            }
            StatusCommand::GetPlayStatus(cmd) => handle_get_play_status(cmd, delegate).await,
            _ => {
                // TODO: remove when we have finish implementing the rest of this enum
                Err(StatusCode::InvalidParameter)
            }
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
                return send_status_response(command, result, pdu_id);
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
    match result {
        Ok(encodable) => match encodable.encode_packet() {
            Ok(packet) => command
                .send_response(AvcResponseType::Accepted, &packet[..])
                .map_err(|e| Error::AvctpError(e)),
            Err(e) => {
                fx_log_err!("Error trying to encode response packet. Sending internal_error rejection to peer {:?}", e);
                send_avc_reject(command, u8::from(&pdu_id), StatusCode::InternalError)
            }
        },
        Err(status_code) => {
            fx_log_err!(
                "Error trying to encode response packet. Sending rejection to peer {:?}",
                status_code
            );
            send_avc_reject(command, u8::from(&pdu_id), status_code)
        }
    }
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

async fn handle_control_command(
    delegate: Arc<TargetDelegate>,
    command: impl IncomingTargetCommand,
    control_command: ControlCommand,
) -> Result<(), Error> {
    let pdu_id = control_command.pdu_id();

    let control_fut = async {
        match control_command {
            /* TODO: Implement
            ControlCommand::SetPlayerApplicationSettingValue(_) => {},
            ControlCommand::RequestContinuingResponse(_) => {},
            ControlCommand::AbortContinuingResponse(_) => {},
            */
            ControlCommand::SetAbsoluteVolume(cmd) => {
                handle_set_absolute_volume(cmd, delegate).await
            }
            _ => {
                // TODO: remove when we have finish implementing the rest of this enum
                Err(StatusCode::InvalidParameter)
            }
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
    use std::sync::atomic::{AtomicBool, Ordering};

    #[derive(Debug)]
    struct MockAvcCommand {
        packet_type: AvcPacketType,
        op_code: AvcOpCode,
        body: Vec<u8>,
        expect_response_type: Option<AvcResponseType>,
        expect_body: Option<Vec<u8>>,
        expect_send: bool,
        send_called: AtomicBool,
    }

    impl MockAvcCommand {
        fn new(packet_type: AvcPacketType, op_code: AvcOpCode, body: Vec<u8>) -> Self {
            Self {
                packet_type,
                op_code,
                body,
                expect_response_type: None,
                expect_body: None,
                expect_send: false,
                send_called: AtomicBool::new(false),
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

        fn expect_interim(self) -> Self {
            self.expect_response_type(AvcResponseType::Interim)
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
            if let Some(expect_response_type) = &self.expect_response_type {
                assert_eq!(&response_type, expect_response_type);
            }

            if let Some(expect_body) = &self.expect_body {
                assert_eq!(body, &expect_body[..]);
            }

            self.send_called.store(true, Ordering::SeqCst);

            Ok(())
        }
    }

    impl Drop for MockAvcCommand {
        fn drop(&mut self) {
            if self.expect_send && !self.send_called.load(Ordering::SeqCst) {
                assert!(false, "AvcCommand::send_response not called");
            }
        }
    }

    /// Creates a simple target handler that responds with error and basic values for most commands.
    fn create_dumby_target_handler(stall_responses: bool) -> TargetHandlerProxy {
        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");

        fasync::spawn(async move {
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
                        assert_eq!(command, AvcPanelCommand::Play);
                        responder.send(&mut Ok(()))
                    }
                    TargetHandlerRequest::ListPlayerApplicationSettingAttributes { responder } => {
                        responder.send(&mut Ok(vec![
                            fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer,
                        ]))
                    }
                    TargetHandlerRequest::GetPlayerApplicationSettings {
                        attribute_ids: _,
                        responder,
                    } => responder.send(&mut Err(TargetAvcError::RejectedInternalError)),
                    TargetHandlerRequest::SetPlayerApplicationSettings {
                        requested_settings: _,
                        responder,
                    } => responder.send(&mut Err(TargetAvcError::RejectedInternalError)),
                    TargetHandlerRequest::GetNotification { event_id: _, responder } => {
                        responder.send(&mut Err(TargetAvcError::RejectedInternalError))
                    }
                    TargetHandlerRequest::WatchNotification {
                        event_id: _,
                        current: _,
                        pos_change_interval: _,
                        responder,
                    } => responder.send(&mut Err(TargetAvcError::RejectedInternalError)),
                };
            }
        });

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

        let cmd_handler = ControlChannelHandler::new(&"test_peer".to_string(), target_delegate);
        cmd_handler
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_get_element_attribute_cmd() -> Result<(), Error> {
        let target_proxy = create_dumby_target_handler(false);
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

    /// send get_capabilities
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_send_get_capabilities() -> Result<(), Error> {
        let target_proxy = create_dumby_target_handler(false);
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
        let target_proxy = create_dumby_target_handler(true);
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
        let target_proxy = create_dumby_target_handler(false);
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

    /// send passthrough is implemented. expect it's accepted
    #[fuchsia_async::run_singlethreaded(test)]
    async fn handle_send_passthrough() -> Result<(), Error> {
        let target_proxy = create_dumby_target_handler(false);
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
        .expect_accept();

        cmd_handler.handle_command_internal(command).await
    }

    #[test]
    fn validate_passthrough_reject_timer_fired() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");

        let target_proxy = create_dumby_target_handler(true);
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
