// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_avctp::{AvcCommand, AvcPeer, AvcResponseType},
    fidl::encoding::Decodable as FidlDecodable,
    fidl::endpoints::{create_endpoints, Proxy, RequestStream, ServiceMarker},
    fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp, *},
    fidl_fuchsia_bluetooth_avrcp_test::*,
    fidl_fuchsia_bluetooth_bredr::PSM_AVCTP,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc, future, future::BoxFuture, future::FutureExt, stream, stream::BoxStream,
        stream::StreamExt,
    },
    parking_lot::Mutex,
    pin_utils::pin_mut,
    std::{collections::VecDeque, convert::TryFrom, sync::Arc},
};

use crate::{
    packets::{
        player_application_settings::PlayerApplicationSettingAttributeId,
        GetCapabilitiesCapabilityId, PlaybackStatus, *,
    },
    peer_manager::PeerManager,
    profile::{
        AvcrpTargetFeatures, AvrcpProfileEvent, AvrcpProtocolVersion, AvrcpService, ProfileService,
    },
    service,
    types::PeerId,
};

pub fn create_fidl_endpoints<S: ServiceMarker>() -> Result<(S::Proxy, S::RequestStream), Error> {
    let (client, server) = zx::Channel::create()?;
    let client = fasync::Channel::from_channel(client)?;
    let client = S::Proxy::from_channel(client);
    let server = fasync::Channel::from_channel(server)?;
    let server = S::RequestStream::from_channel(server);
    Ok((client, server))
}

#[derive(Debug)]
pub struct MockProfileServiceState {
    pub fake_events: Mutex<Option<VecDeque<Result<AvrcpProfileEvent, Error>>>>,
    pub fake_connect_to_device_response: Mutex<Option<Result<zx::Socket, Error>>>,
}

#[derive(Debug)]
pub struct MockProfileService {
    inner: Arc<MockProfileServiceState>,
}

impl ProfileService for MockProfileService {
    fn connect_to_device<'a>(
        &'a self,
        _peer_id: &'a PeerId,
        _psm: u16,
    ) -> BoxFuture<Result<zx::Socket, Error>> {
        let result = self.inner.fake_connect_to_device_response.lock().take();

        match result {
            Some(result_value) => future::ready(result_value).boxed(),
            None => future::ready(Err(format_err!("no value"))).boxed(),
        }
    }

    fn take_event_stream(&self) -> BoxStream<Result<AvrcpProfileEvent, Error>> {
        let value = self.inner.fake_events.lock().take();
        match value {
            Some(events) => stream::iter(events).boxed(),
            None => panic!("No fake events"),
        }
    }
}

impl MockProfileService {
    pub fn new(
        events: Option<VecDeque<Result<AvrcpProfileEvent, Error>>>,
        device_response: Option<Result<zx::Socket, Error>>,
    ) -> (Self, Arc<MockProfileServiceState>) {
        let state = Arc::new(MockProfileServiceState {
            fake_events: Mutex::new(events),
            fake_connect_to_device_response: Mutex::new(device_response),
        });
        (Self { inner: state.clone() }, state)
    }
}

fn spawn_peer_manager() -> Result<PeerManagerProxy, Error> {
    let (client_sender, service_request_receiver) = mpsc::channel(2);
    let (profile_service, _) = MockProfileService::new(Some(VecDeque::new()), None);

    let peer_manager = PeerManager::new(Box::new(profile_service), service_request_receiver)
        .expect("unable to create peer manager");

    let (peer_manager_client, peer_manager_client_stream): (
        PeerManagerProxy,
        PeerManagerRequestStream,
    ) = create_fidl_endpoints::<PeerManagerMarker>()?;

    fasync::spawn(async move {
        let _ = service::avrcp_client_stream_handler(
            peer_manager_client_stream,
            client_sender.clone(),
            &service::spawn_avrcp_client_controller,
        )
        .await;
    });

    fasync::spawn(async move {
        let _ = peer_manager.run().await;
    });

    Ok(peer_manager_client)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn target_delegate_target_handler_already_bound_test() -> Result<(), Error> {
    let peer_manager_client = spawn_peer_manager()?;

    // create an target volume handler.
    let (target_client_end_1, target_server_end_1) =
        create_endpoints::<TargetHandlerMarker>().expect("Error creating TargetHandler endpoint");

    // first set should succeed
    assert_eq!(
        peer_manager_client
            .register_target_handler(target_client_end_1)
            .await
            .expect("unexpected FIDL error"),
        Ok(())
    );

    // drop the first one. the remote should drop handler when it notices the handle has be closed.
    drop(target_server_end_1);

    // create an new target handler.
    let (target_client_end_2, target_server_end_2) =
        create_endpoints::<TargetHandlerMarker>().expect("Error creating TargeteHandler endpoint");

    // should succeed if the previous handler was dropped.
    assert_eq!(
        peer_manager_client
            .register_target_handler(target_client_end_2)
            .await
            .expect("unexpected FIDL error"),
        Ok(())
    );

    // create an new target handler.
    let (target_client_end_3, target_server_end_3) =
        create_endpoints::<TargetHandlerMarker>().expect("Error creating TargetHandler endpoint");

    // should fail since the target handler is already set.
    assert_eq!(
        peer_manager_client
            .register_target_handler(target_client_end_3)
            .await
            .expect("unexpected FIDL error"),
        Err(zx::Status::ALREADY_BOUND.into_raw())
    );

    drop(target_server_end_2);
    drop(target_server_end_3);
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn target_delegate_volume_handler_already_bound_test() -> Result<(), Error> {
    let peer_manager_client = spawn_peer_manager()?;

    // create an absolute volume handler.
    let (absolute_volume_client_end_1, absolute_volume_server_end_1) =
        create_endpoints::<AbsoluteVolumeHandlerMarker>()
            .expect("Error creating AbsoluteVolumeHandler endpoint");

    // first set should succeed
    assert_eq!(
        peer_manager_client
            .set_absolute_volume_handler("", absolute_volume_client_end_1)
            .await
            .expect("unexpected FIDL error"),
        Ok(())
    );

    // drop the first one. the remote should drop handler when it notices the handle has be closed.
    drop(absolute_volume_server_end_1);

    // create an new absolute volume handler.
    let (absolute_volume_client_end_2, absolute_volume_server_end_2) =
        create_endpoints::<AbsoluteVolumeHandlerMarker>()
            .expect("Error creating AbsoluteVolumeHandler endpoint");

    // should succeed if the previous handler was dropped.
    assert_eq!(
        peer_manager_client
            .set_absolute_volume_handler("", absolute_volume_client_end_2)
            .await
            .expect("unexpected FIDL error"),
        Ok(())
    );

    // create an new absolute volume handler.
    let (absolute_volume_client_end_3, absolute_volume_server_end_3) =
        create_endpoints::<AbsoluteVolumeHandlerMarker>()
            .expect("Error creating AbsoluteVolumeHandler endpoint");

    // should fail since the volume handler is already set.
    assert_eq!(
        peer_manager_client
            .set_absolute_volume_handler("", absolute_volume_client_end_3)
            .await
            .expect("unexpected FIDL error"),
        Err(zx::Status::ALREADY_BOUND.into_raw())
    );

    drop(absolute_volume_server_end_2);
    drop(absolute_volume_server_end_3);

    Ok(())
}

// Integration test of the peer manager and the FIDL front end with a mock BDEDR backend an
// emulated remote peer. Validates we can get a controller to a device we discovered, we can send
// commands on that controller, and that we can send responses and have them be dispatched back as
// responses to the FIDL frontend in AVRCP. Exercises a majority of the peer manager
// controller logic.
// 1. Creates a front end FIDL endpoints for the test controller interface, a peer manager, and mock
//    backend.
// 2. It then creates a socket and injects a fake services discovered and incoming connection
//    event into the mock profile service.
// 3. Obtains both a regular and test controller from the FIDL service.
// 4. Issues a Key0 passthrough keypress and validates we got both a key up and key down event
// 4. Issues a GetCapabilities command using get_events_supported on the test controller FIDL
// 5. Issues a SetAbsoluteVolume command on the controller FIDL
// 6. Issues a GetElementAttributes command, encodes multiple packets, and handles continuations
// 7. Issues a GetPlayStatus command on the controller FIDL.
// 8. Issues a GetPlayerApplicationSettings command on the controller FIDL.
// 9. Issues a SetPlayerApplicationSettings command on the controller FIDL.
// 10. Register event notification for position change callbacks and mock responses.
// 11. Waits until we have a response to all commands from our mock remote service return expected
//    values and we have received enough position change events.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_spawn_peer_manager_with_fidl_client_and_mock_profile() -> Result<(), Error> {
    const REQUESTED_VOLUME: u8 = 0x40;
    const SET_VOLUME: u8 = 0x24;
    const FAKE_PEER_ID: &str = "1234";
    const LOREM_IPSUM: &str = "Lorem ipsum dolor sit amet,\
         consectetur adipiscing elit. Nunc eget elit cursus ipsum \
         fermentum viverra id vitae lorem. Cras luctus elementum \
         metus vel volutpat. Vestibulum ante ipsum primis in \
         faucibus orci luctus et ultrices posuere cubilia \
         Curae; Praesent efficitur velit sed metus luctus \
         Mauris in ante ultrices, vehicula lorem non, sagittis metus. \
         Nam facilisis volutpat quam. Suspendisse sem ipsum, blandit ut faucibus vitae,\
         facilisis quis massa. Aliquam sagittis, orci sed dignissim vulputate, odio neque \
         tempor dui, vel feugiat metus massa id urna. Nam at risus sem.\
         Duis commodo suscipit metus, at placerat elit suscipit eget. Suspendisse interdum \
         id metus vitae porta. Ut cursus viverra imperdiet. Aliquam erat volutpat. \
         Curabitur vehicula mauris nec ex sollicitudin rhoncus. Integer ipsum libero, \
         porta id velit et, egestas facilisis tellus.\
         Mauris in ante ultrices, vehicula lorem non, sagittis metus.\
         Nam facilisis volutpat quam. Suspendisse sem ipsum, blandit ut faucibus vitae,\
         facilisis quis massa. Aliquam sagittis, orci sed dignissim vulputate, odio neque \
         tempor dui, vel feugiat metus massa id urna. Nam at risus sem.\
         Duis commodo suscipit metus, at placerat elit suscipit eget. Suspendisse interdum \
         id metus vitae porta. Ut cursus viverra imperdiet. Aliquam erat volutpat. \
         Curabitur vehicula mauris nec ex sollicitudin rhoncus. Integer ipsum libero, \
         porta id velit et, egestas facilisis tellus.";

    // when zero, we exit the test.
    let mut expected_commands: i64 = 0;

    let (c_client, c_server): (PeerManagerProxy, PeerManagerRequestStream) =
        create_fidl_endpoints::<PeerManagerMarker>()?;

    let (t_client, t_server): (PeerManagerExtProxy, PeerManagerExtRequestStream) =
        create_fidl_endpoints::<PeerManagerExtMarker>()?;

    let (client_sender, peer_controller_request_receiver) = mpsc::channel(512);

    let (local, remote) =
        zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("unable to make socket");

    // Queue up two fake test events.
    let mut events = VecDeque::new();
    events.push_back(Ok(AvrcpProfileEvent::ServicesDiscovered {
        peer_id: FAKE_PEER_ID.to_string(),
        services: vec![AvrcpService::Target {
            features: AvcrpTargetFeatures::CATEGORY1,
            psm: PSM_AVCTP as u16,
            protocol_version: AvrcpProtocolVersion(1, 6),
        }],
    }));
    events.push_back(Ok(AvrcpProfileEvent::IncomingControlConnection {
        peer_id: FAKE_PEER_ID.to_string(),
        channel: local,
    }));

    let remote_peer = AvcPeer::new(remote)?;

    let (profile_service, _state) = MockProfileService::new(Some(events), None);

    let peer_manager =
        PeerManager::new(Box::new(profile_service), peer_controller_request_receiver)
            .expect("unable to create peer manager");

    let (controller_client_endpoint, controller_server_endpoint) =
        create_endpoints::<ControllerMarker>().expect("Error creating Controller endpoint");

    let (controller_ext_client_endpoint, controller_ext_server_endpoint) =
        create_endpoints::<ControllerExtMarker>().expect("Error creating Test Controller endpoint");

    let handler_fut = service::avrcp_client_stream_handler(
        c_server,
        client_sender.clone(),
        &service::spawn_avrcp_client_controller,
    )
    .fuse();
    pin_mut!(handler_fut);

    let test_handler_fut = service::test_avrcp_client_stream_handler(
        t_server,
        client_sender.clone(),
        &service::spawn_test_avrcp_client_controller,
    )
    .fuse();
    pin_mut!(test_handler_fut);

    let get_controller_fut =
        c_client.get_controller_for_target(FAKE_PEER_ID, controller_server_endpoint).fuse();
    pin_mut!(get_controller_fut);

    let get_test_controller_fut =
        t_client.get_controller_for_target(FAKE_PEER_ID, controller_ext_server_endpoint).fuse();
    pin_mut!(get_test_controller_fut);

    let peer_manager_run_fut = peer_manager.run().fuse();
    pin_mut!(peer_manager_run_fut);

    let controller_proxy = controller_client_endpoint.into_proxy()?;
    let controller_ext_proxy = controller_ext_client_endpoint.into_proxy()?;

    let is_connected_fut = controller_ext_proxy.is_connected().fuse();
    pin_mut!(is_connected_fut);

    let passthrough_fut = controller_proxy.send_command(AvcPanelCommand::Key0).fuse();
    pin_mut!(passthrough_fut);
    expected_commands += 1;
    let mut keydown_pressed = false;
    let mut keyup_pressed = false;

    let volume_fut = controller_proxy.set_absolute_volume(REQUESTED_VOLUME).fuse();
    pin_mut!(volume_fut);
    expected_commands += 1;

    let events_fut = controller_ext_proxy.get_events_supported().fuse();
    pin_mut!(events_fut);
    expected_commands += 1;

    let get_media_attributes_fut = controller_proxy.get_media_attributes().fuse();
    pin_mut!(get_media_attributes_fut);
    expected_commands += 1;

    let get_play_status_fut = controller_proxy.get_play_status().fuse();
    pin_mut!(get_play_status_fut);
    expected_commands += 1;

    let attribute_ids = vec![fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer];
    let get_player_application_settings_fut =
        controller_proxy.get_player_application_settings(&mut attribute_ids.into_iter()).fuse();
    pin_mut!(get_player_application_settings_fut);
    expected_commands += 1;

    let attribute_ids_empty = vec![];
    let get_all_player_application_settings_fut = controller_proxy
        .get_player_application_settings(&mut attribute_ids_empty.into_iter())
        .fuse();
    pin_mut!(get_all_player_application_settings_fut);
    expected_commands += 1;

    let mut settings = fidl_avrcp::PlayerApplicationSettings::new_empty();
    settings.scan_mode = Some(fidl_avrcp::ScanMode::GroupScan);
    settings.shuffle_mode = Some(fidl_avrcp::ShuffleMode::Off);
    let set_player_application_settings_fut =
        controller_proxy.set_player_application_settings(settings).fuse();
    pin_mut!(set_player_application_settings_fut);
    expected_commands += 1;

    let mut additional_packets: Vec<Vec<u8>> = vec![];

    // set controller event filter to ones we support.
    let _ = controller_proxy
        .set_notification_filter(Notifications::TrackPos | Notifications::Volume, 1)?;

    let mut volume_value_received = false;

    let event_stream = controller_proxy.take_event_stream();
    pin_mut!(event_stream);

    let mut position_changed_events = 0;

    let remote_command_stream = remote_peer.take_command_stream().fuse();
    pin_mut!(remote_command_stream);

    let mut handle_remote_command = |avc_command: AvcCommand| {
        if avc_command.is_vendor_dependent() {
            let packet_body = avc_command.body();

            let preamble =
                VendorDependentPreamble::decode(packet_body).expect("unable to decode packet");

            let body = &packet_body[preamble.encoded_len()..];

            let pdu_id = PduId::try_from(preamble.pdu_id).expect("unknown PDU_ID");

            match pdu_id {
                PduId::GetElementAttributes => {
                    let _get_element_attributes_command = GetElementAttributesCommand::decode(body)
                        .expect("unable to decode get element attributes");

                    // We are making a massive response to test packet continuations work properly.
                    let element_attributes_response = GetElementAttributesResponse {
                        title: Some(String::from(&LOREM_IPSUM[0..100])),
                        artist_name: Some(String::from(&LOREM_IPSUM[500..])),
                        album_name: Some(String::from(&LOREM_IPSUM[250..500])),
                        genre: Some(String::from(&LOREM_IPSUM[100..250])),
                        ..GetElementAttributesResponse::default()
                    };
                    let mut packets = element_attributes_response
                        .encode_packets()
                        .expect("unable to encode packets for event");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &packets[0][..]);
                    packets.remove(0);
                    additional_packets = packets;
                }
                PduId::RequestContinuingResponse => {
                    let request_cont_response = RequestContinuingResponseCommand::decode(body)
                        .expect("unable to decode continuting response");
                    assert_eq!(request_cont_response.pdu_id_response(), 0x20); // GetElementAttributes

                    if additional_packets.len() > 1 {
                        let _ = avc_command.send_response(
                            AvcResponseType::ImplementedStable,
                            &additional_packets[0][..],
                        );
                    } else {
                        let _ = avc_command.send_response(
                            AvcResponseType::ImplementedStable,
                            &additional_packets[0][..],
                        );
                    }
                    additional_packets.remove(0);
                }
                PduId::RegisterNotification => {
                    let register_notification_command = RegisterNotificationCommand::decode(body)
                        .expect("unable to decode packet body");

                    assert!(
                        register_notification_command.event_id()
                            == &NotificationEventId::EventPlaybackPosChanged
                            || register_notification_command.event_id()
                                == &NotificationEventId::EventVolumeChanged
                    );

                    match register_notification_command.event_id() {
                        &NotificationEventId::EventPlaybackPosChanged => {
                            position_changed_events += 1;

                            let intirm_response = PlaybackPosChangedNotificationResponse::new(
                                1000 * position_changed_events,
                            )
                            .encode_packet()
                            .expect("unable to encode pos response packet");
                            let _ = avc_command
                                .send_response(AvcResponseType::Interim, &intirm_response[..]);

                            // we are going to hang the response and not return an changed response after the 50th call.
                            // the last interim response should be
                            if position_changed_events < 50 {
                                let change_response = PlaybackPosChangedNotificationResponse::new(
                                    1000 * (position_changed_events + 1),
                                )
                                .encode_packet()
                                .expect("unable to encode pos response packet");
                                let _ = avc_command
                                    .send_response(AvcResponseType::Changed, &change_response[..]);
                            }
                        }
                        &NotificationEventId::EventVolumeChanged => {
                            let intirm_response = VolumeChangedNotificationResponse::new(0x22)
                                .encode_packet()
                                .expect("unable to encode volume response packet");
                            let _ = avc_command
                                .send_response(AvcResponseType::Interim, &intirm_response[..]);

                            // we do not send an change response. We just hang it as if the volume never changes.
                        }
                        _ => assert!(false),
                    }
                }
                PduId::SetAbsoluteVolume => {
                    let set_absolute_volume_command =
                        SetAbsoluteVolumeCommand::decode(body).expect("unable to packet body");
                    assert_eq!(set_absolute_volume_command.volume(), REQUESTED_VOLUME);
                    let response = SetAbsoluteVolumeResponse::new(SET_VOLUME)
                        .expect("volume error")
                        .encode_packet()
                        .expect("unable to encode volume response packet");
                    let _ = avc_command.send_response(AvcResponseType::Accepted, &response[..]);
                }
                PduId::GetCapabilities => {
                    let get_capabilities_command =
                        GetCapabilitiesCommand::decode(body).expect("unable to packet body");
                    assert_eq!(
                        get_capabilities_command.capability_id(),
                        GetCapabilitiesCapabilityId::EventsId
                    );
                    // only notification types we claim to have are battery status, track pos, and volume
                    let response = GetCapabilitiesResponse::new_events(&[0x05, 0x06, 0x0d])
                        .encode_packet()
                        .expect("unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::GetPlayStatus => {
                    let _get_play_status_comand = GetPlayStatusCommand::decode(body)
                        .expect("GetPlayStatus: unable to packet body");
                    // Reply back with arbitrary status response. Song pos not supported.
                    let response =
                        GetPlayStatusResponse::new(100, 0xFFFFFFFF, PlaybackStatus::Stopped)
                            .encode_packet()
                            .expect("unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::GetCurrentPlayerApplicationSettingValue => {
                    let _get_player_application_settings_command =
                        GetCurrentPlayerApplicationSettingValueCommand::decode(body)
                            .expect("GetPlayerApplicationSettings: unable to packet body");
                    let response = GetCurrentPlayerApplicationSettingValueResponse::new(vec![(
                        player_application_settings::PlayerApplicationSettingAttributeId::Equalizer,
                        0x01,
                    )])
                    .encode_packet()
                    .expect("Unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::SetPlayerApplicationSettingValue => {
                    let _set_player_application_settings_command =
                        SetPlayerApplicationSettingValueCommand::decode(body)
                            .expect("SetPlayerApplicationSettings: unable to packet body");
                    let response = SetPlayerApplicationSettingValueResponse::new()
                        .encode_packet()
                        .expect("Unable to encode response");
                    let _ = avc_command.send_response(AvcResponseType::Accepted, &response[..]);
                }
                PduId::ListPlayerApplicationSettingAttributes => {
                    let _list_attributes_command =
                        ListPlayerApplicationSettingAttributesCommand::decode(body).expect(
                            "ListPlayerApplicationSettingAttributes: unable to packet body",
                        );
                    let response = ListPlayerApplicationSettingAttributesResponse::new(
                        1,
                        vec![PlayerApplicationSettingAttributeId::Equalizer],
                    )
                    .encode_packet()
                    .expect("Unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::GetPlayerApplicationSettingAttributeText => {
                    let _get_attribute_text_command =
                        GetPlayerApplicationSettingAttributeTextCommand::decode(body).expect(
                            "GetPlayerApplicationSettingAttributeText: unable to packet body",
                        );
                    let response = GetPlayerApplicationSettingAttributeTextResponse::new(vec![
                        AttributeInfo::new(
                            PlayerApplicationSettingAttributeId::Equalizer,
                            CharsetId::Utf8,
                            1,
                            vec![0x62],
                        ),
                    ])
                    .encode_packet()
                    .expect("Unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::ListPlayerApplicationSettingValues => {
                    let _list_value_command =
                        ListPlayerApplicationSettingValuesCommand::decode(body)
                            .expect("ListPlayerApplicationSettingValues: unable to packet body");
                    let response =
                        ListPlayerApplicationSettingValuesResponse::new(2, vec![0x01, 0x02])
                            .encode_packet()
                            .expect("Unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                PduId::GetPlayerApplicationSettingValueText => {
                    let _get_value_text_command =
                        GetPlayerApplicationSettingValueTextCommand::decode(body)
                            .expect("GetPlayerApplicationSettingValueText: unable to packet body");
                    let response =
                        GetPlayerApplicationSettingValueTextResponse::new(vec![ValueInfo::new(
                            1,
                            CharsetId::Utf8,
                            1,
                            vec![0x63],
                        )])
                        .encode_packet()
                        .expect("Unable to encode response");
                    let _ = avc_command
                        .send_response(AvcResponseType::ImplementedStable, &response[..]);
                }
                _ => {
                    // not entirely correct but just get it off our back for now.
                    let _ = avc_command.send_response(AvcResponseType::NotImplemented, &[]);
                }
            }
        } else {
            // Passthrough
            let body = avc_command.body();
            if body[0] & 0x80 == 0 {
                keydown_pressed = true;
            } else {
                keyup_pressed = true;
            }
            let key_code = body[0] & !0x80;
            let command = AvcPanelCommand::from_primitive(key_code);
            assert_eq!(command, Some(AvcPanelCommand::Key0));
            let _ = avc_command.send_response(AvcResponseType::Accepted, &[]);
        }
    };

    let mut last_receieved_pos = 0;

    loop {
        futures::select! {
            command = remote_command_stream.select_next_some() => {
                handle_remote_command(command?);
            }
            _= peer_manager_run_fut  => {
                return Err(format_err!("peer manager returned early"))
            }
            res = handler_fut => {
                let _ = res?;
                assert!(false, "handler returned");
                drop(c_client);
                return Ok(())
            }
            res = test_handler_fut => {
                let _ = res?;
                assert!(false, "handler returned");
                drop(t_client);
                return Ok(())
            }
            res = get_controller_fut => {
                let _ = res?;
            }
            res = get_test_controller_fut => {
                let _ = res?;
            }
            res = is_connected_fut => {
                assert!(res? == true, "connected should return with true");
            }
            res = passthrough_fut => {
                expected_commands -= 1;
                assert_eq!(res?, Ok(()));
            }
            res = volume_fut => {
                expected_commands -= 1;
                assert_eq!(res?, Ok(SET_VOLUME));
            }
            res = events_fut => {
                expected_commands -= 1;
                assert_eq!(res?, Ok(vec![NotificationEvent::TrackPosChanged, NotificationEvent::BattStatusChanged, NotificationEvent::VolumeChanged]));
            }
            res = get_media_attributes_fut => {
                expected_commands -= 1;
                let media_attributes = res?.expect("unable to parse media attributes");
                let expected = Some(String::from(&LOREM_IPSUM[100..250]));
                assert_eq!(media_attributes.genre, expected);
            }
            res = get_play_status_fut => {
                expected_commands -= 1;
                let play_status = res?.expect("unable to parse play status");
                assert_eq!(play_status.playback_status, Some(fidl_avrcp::PlaybackStatus::Stopped).into());
                assert_eq!(play_status.song_length, Some(100));
                assert_eq!(play_status.song_position, None);
            }
            res = get_player_application_settings_fut => {
                expected_commands -= 1;
                let settings = res?.expect("unable to parse get player application settings");
                assert!(settings.equalizer.is_some());
                assert!(settings.custom_settings.is_none());
                assert!(settings.scan_mode.is_none());
                assert!(settings.repeat_status_mode.is_none());
                assert!(settings.shuffle_mode.is_none());
                let eq = settings.equalizer.unwrap();
                assert_eq!(eq, fidl_avrcp::Equalizer::Off);
            }
            res = get_all_player_application_settings_fut => {
                expected_commands -= 1;
                let settings = res?.expect("unable to parse get player application settings");
                assert!(settings.equalizer.is_some());
                assert_eq!(settings.equalizer.unwrap(), fidl_avrcp::Equalizer::Off);
            }
            res = set_player_application_settings_fut => {
                expected_commands -= 1;
                let set_settings = res?.expect("unable to parse set player application settings");
                assert_eq!(set_settings.scan_mode, Some(fidl_avrcp::ScanMode::GroupScan));
                assert_eq!(set_settings.shuffle_mode, Some(fidl_avrcp::ShuffleMode::Off));
            }
            event = event_stream.select_next_some() => {
                match event? {
                    ControllerEvent::OnNotification { timestamp, notification } => {
                        if let Some(value) = notification.pos {
                            last_receieved_pos = value;
                        }

                        if let Some(value) = notification.volume {
                            volume_value_received = true;
                            assert_eq!(value, 0x22);
                        }

                        controller_proxy.notify_notification_handled()?;
                    }
                }
            }
        }
        if expected_commands <= 0 && last_receieved_pos == 50 * 1000 && volume_value_received {
            assert!(keydown_pressed && keyup_pressed);
            return Ok(());
        }
    }
}
