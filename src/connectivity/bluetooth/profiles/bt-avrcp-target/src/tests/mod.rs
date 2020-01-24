// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    anyhow::Error,
    fidl::encoding::Decodable as FidlDecodable,
    fidl::endpoints::create_proxy_and_stream,
    fidl_fuchsia_bluetooth_avrcp::*,
    fidl_fuchsia_media_sessions2::*,
    fuchsia_async as fasync,
    futures::TryStreamExt,
    futures::{pin_mut, task::Poll},
};

use fidl::client::QueryResponseFut;
use fidl_fuchsia_bluetooth_avrcp::{
    TargetHandlerMarker, TargetHandlerProxy, TargetHandlerRequestStream,
    TargetHandlerWatchNotificationResponder, TargetHandlerWatchNotificationResult,
};
use futures::StreamExt;

use crate::avrcp_handler::handle_target_requests;
use crate::media::media_state::tests::*;

fn setup_target_handler() -> (TargetHandlerProxy, TargetHandlerRequestStream) {
    create_proxy_and_stream::<TargetHandlerMarker>().expect("Should work")
}

fn setup_sessions_watcher() -> (SessionsWatcherProxy, SessionsWatcherRequestStream) {
    create_proxy_and_stream::<SessionsWatcherMarker>().expect("Should work")
}

/// Makes a call to TargetHandler::WatchNotification to generate a FIDL
/// responder.
///
/// The arguments to `WatchNotification` are there as placeholders and
/// have no functional impact on the tests.
pub async fn generate_empty_watch_notification(
    proxy: &mut TargetHandlerProxy,
    stream: &mut TargetHandlerRequestStream,
) -> Result<
    (
        QueryResponseFut<TargetHandlerWatchNotificationResult>,
        TargetHandlerWatchNotificationResponder,
    ),
    Error,
> {
    let dummy_id = NotificationEvent::TrackChanged;
    let dummy_current = Notification::new_empty();
    let interval: u32 = 0;

    let result_fut = proxy.watch_notification(dummy_id, dummy_current, interval);
    let (_, _, _, responder) = stream
        .next()
        .await
        .expect("FIDL call should work")
        .expect("FIDL call should return ok")
        .into_watch_notification()
        .expect("Watch notification request");

    Ok((result_fut, responder))
}

async fn send_get_player_application_settings(target_proxy: TargetHandlerProxy) {
    // Send a GetPlayerApplicationSettings for the active session.
    let attribute_ids = vec![]; // Get all supported.
    let res = target_proxy
        .get_player_application_settings(&mut attribute_ids.into_iter())
        .await
        .expect("FIDL call should work");
    assert_eq!(
        Ok(PlayerApplicationSettings {
            repeat_status_mode: Some(RepeatStatusMode::GroupRepeat),
            shuffle_mode: Some(ShuffleMode::Off),
            ..PlayerApplicationSettings::new_empty()
        }),
        res
    );

    // Request an unsupported PlayerApplicationSetting.
    let attribute_ids = vec![PlayerApplicationSettingAttributeId::Equalizer];
    let res = target_proxy
        .get_player_application_settings(&mut attribute_ids.into_iter())
        .await
        .expect("FIDL call should work");
    assert_eq!(Err(TargetAvcError::RejectedInvalidParameter), res);
}

async fn send_set_player_application_settings(target_proxy: TargetHandlerProxy) {
    // Send an unsupported setting among supported settings.
    let requested_settings = PlayerApplicationSettings {
        equalizer: Some(Equalizer::On), // Unsupported
        shuffle_mode: Some(ShuffleMode::GroupShuffle),
        ..PlayerApplicationSettings::new_empty()
    };
    let res = target_proxy
        .set_player_application_settings(requested_settings)
        .await
        .expect("FIDL call should work");
    assert_eq!(Err(TargetAvcError::RejectedInvalidParameter), res);

    // The returned set settings will differ from the `requested_settings` because
    // there is not a 1:1 mapping of AVRCP to Media types.
    let requested_settings = PlayerApplicationSettings {
        shuffle_mode: Some(ShuffleMode::GroupShuffle),
        ..PlayerApplicationSettings::new_empty()
    };
    let res = target_proxy
        .set_player_application_settings(requested_settings)
        .await
        .expect("FIDL call should work");
    let expected_response = PlayerApplicationSettings {
        shuffle_mode: Some(ShuffleMode::AllTrackShuffle),
        ..PlayerApplicationSettings::new_empty()
    };
    assert_eq!(Ok(expected_response), res);
}

async fn send_panel_commands(target_proxy: TargetHandlerProxy) {
    // Send Panel Commands.
    let res = target_proxy
        .send_command(AvcPanelCommand::FastForward, true) // Supported
        .await
        .expect("FIDL call should work");
    assert_eq!(Ok(()), res.map_err(|e| format!("{:?}", e)));
    let res = target_proxy
        .send_command(AvcPanelCommand::List, true) // Unsupported
        .await
        .expect("FIDL call should work");
    assert_eq!(Err(TargetPassthroughError::CommandNotImplemented), res);

    let res = target_proxy
        .send_command(AvcPanelCommand::Pause, false) // Supported
        .await
        .expect("FIDL call should work");
    assert_eq!(Ok(()), res);
}

async fn send_get_notification(target_proxy: TargetHandlerProxy) {
    // Send a GetNotification request for the active session.
    let res = target_proxy
        .get_notification(NotificationEvent::TrackPosChanged)
        .await
        .expect("FIDL call should work");
    assert_eq!(Ok(Notification { pos: Some(std::u32::MAX), ..Notification::new_empty() }), res);

    // Send a GetNotification request for the active session.
    let res = target_proxy
        .get_notification(NotificationEvent::TrackChanged)
        .await
        .expect("FIDL call should work");
    assert_eq!(
        Ok(Notification { track_id: Some(std::u64::MAX), ..Notification::new_empty() }),
        res
    );

    // Send an unsupported `NotificationEvent`.
    let res = target_proxy
        .get_notification(NotificationEvent::AvailablePlayersChanged)
        .await
        .expect("FIDL call should work");
    assert_eq!(Err(TargetAvcError::RejectedInvalidParameter), res);
}

async fn send_get_play_status(target_proxy: TargetHandlerProxy) {
    let res = target_proxy.get_play_status().await.expect("FIDL call should work");
    assert_eq!(
        Ok(PlayStatus {
            song_length: Some(123),
            song_position: Some(55), // Predetermined time using exec.set_fake_time().
            playback_status: Some(PlaybackStatus::Playing),
        }),
        res
    );
}

async fn send_get_media_attributes(target_proxy: TargetHandlerProxy) {
    let res = target_proxy.get_media_attributes().await.expect("FIDL call should work");
    assert_eq!(
        Ok(MediaAttributes {
            title: Some("This is a sample title".to_string()),
            artist_name: None,
            album_name: None,
            track_number: None,
            total_number_of_tracks: None,
            genre: None,
            playing_time: Some("123".to_string()),
        }),
        res
    );
}

async fn handle_session_control_requests(mut stream: SessionControlRequestStream) {
    while let Some(req) = stream.try_next().await.expect("Failed to serve session control") {
        match req {
            SessionControlRequest::Pause { .. } | SessionControlRequest::SetShuffleMode { .. } => {}
            _ => panic!("Received unexpected SessionControl request."),
        }
    }
}

async fn handle_discovery_requests(mut stream: DiscoveryRequestStream) {
    while let Some(req) = stream.try_next().await.expect("Failed to serve session control") {
        match req {
            DiscoveryRequest::WatchSessions { .. } => {}
            DiscoveryRequest::ConnectToSession { session_control_request, .. } => {
                // For new sessions, spawn a mock listener that acknowledges proxied commands.
                let request_stream = session_control_request
                    .into_stream()
                    .expect("Failed to take session control request stream");
                fasync::spawn(handle_session_control_requests(request_stream));
            }
        }
    }
}

#[test]
/// Mock the TargetHandler request stream and send procedures over the proxy.
/// Mock the MediaSession SessionsWatcher and send `SessionUpdated` over the channel to
/// ensure state updates are correctly received and updated.
/// Tests that multiple MediaSessions are appropriately handled, including the setting of
/// the "currently active session".
/// This test simulates end-to-end behavior of the AVRCP Target component. It tests a
/// fake client (usually AVRCP component) sending procedures and verifies the results.
fn test_media_and_avrcp_listener() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["avrcp-tg-test"]).expect("Unable to initialize logger");
    fuchsia_syslog::set_verbosity(1);
    let mut exec = fasync::Executor::new_with_fake_time().expect("executor should build");
    exec.set_fake_time(fasync::Time::from_nanos(555555555));

    let test_fut = async {
        // AVRCP
        let (target_proxy, target_request_stream) = setup_target_handler();

        // Media
        let (discovery, discovery_request_stream) = create_proxy_and_stream::<DiscoveryMarker>()
            .expect("Couldn't create discovery service");
        let (watcher_client, watcher_request_stream) = setup_sessions_watcher();

        // Spawn the mocked MediaSessions server (sends canned responses).
        fasync::spawn(handle_discovery_requests(discovery_request_stream));

        // Create local state for testing.
        let media_sessions: Arc<MediaSessions> = Arc::new(MediaSessions::create());
        let media_sessions_copy = media_sessions.clone();
        let media_sessions_copy2 = media_sessions.clone();
        assert_eq!(None, media_sessions.get_active_session_id());

        // Spawn the AVRCP request task.
        fasync::spawn(async move {
            let _ = handle_target_requests(target_request_stream, media_sessions_copy).await;
        });

        // Spawn the Media listener task.
        fasync::spawn(async move {
            let _ = media_sessions_copy2
                .watch_media_sessions(discovery.clone(), watcher_request_stream)
                .await;
        });

        // Integration tests begin.
        // Get TG supported events (static response, not contingent on active media player).
        let res = target_proxy.get_events_supported().await.expect("FIDL call should work");
        assert_eq!(
            Ok(vec![
                NotificationEvent::PlayerApplicationSettingChanged,
                NotificationEvent::PlaybackStatusChanged,
                NotificationEvent::TrackChanged,
                NotificationEvent::TrackPosChanged,
            ]),
            res
        );

        // Mock two sessions.
        let session1_id: u64 = 1234;
        let session2_id: u64 = 9876;

        // Get the play status of the active session.
        // There is no active session, so this should be rejected.
        let res = target_proxy.get_play_status().await.expect("FIDL call should work");
        assert_eq!(Err(TargetAvcError::RejectedNoAvailablePlayers), res);

        // Send a MediaSessionUpdate for session1 -> New active session.
        let delta1 = SessionInfoDelta { ..SessionInfoDelta::new_empty() };
        let _ = watcher_client.session_updated(session1_id, delta1).await;
        assert_eq!(Some(session1_id), media_sessions.get_active_session_id());

        // Get MediaSession supported player application setting attributes. This is
        // a static response, but it is defined per MediaSession, so in the future, it can
        // be a dynamic result that differs per session.
        let res = target_proxy
            .list_player_application_setting_attributes()
            .await
            .expect("FIDL call should work");
        assert_eq!(
            Ok(vec![
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
                PlayerApplicationSettingAttributeId::ShuffleMode,
            ]),
            res
        );

        // Send a MediaSessionUpdate for session2 -> New active session. Replaces session1.
        let delta2 = SessionInfoDelta {
            metadata: Some(create_metadata()),
            player_status: Some(create_player_status()),
            ..SessionInfoDelta::new_empty()
        };
        let _ = watcher_client.session_updated(session2_id, delta2).await;
        assert_eq!(Some(session2_id), media_sessions.get_active_session_id());

        // Test getting the media info of the active session.
        send_get_media_attributes(target_proxy.clone()).await;

        // Test getting the play status of the active session.
        send_get_play_status(target_proxy.clone()).await;

        // Send a MediaSessionUpdate for session2 -> Paused, so now it's not active.
        // As per MediaSession, when a player is paused, it becomes inactive, and a
        // SessionRemoved should be triggered.
        // Then, make session1 the new active session.
        let res = watcher_client.session_removed(session2_id).await;
        assert_eq!(Ok(()), res.map_err(|e| format!("{}", e)));
        assert_eq!(None, media_sessions.get_active_session_id());

        let delta1 = SessionInfoDelta {
            player_status: Some(PlayerStatus {
                duration: Some(9876543210),
                player_state: Some(PlayerState::Playing),
                repeat_mode: Some(RepeatMode::Group),
                ..PlayerStatus::new_empty()
            }),
            ..SessionInfoDelta::new_empty()
        };
        let _ = watcher_client.session_updated(session1_id, delta1).await;
        assert_eq!(Some(session1_id), media_sessions.get_active_session_id());

        // Test getting player application settings.
        send_get_player_application_settings(target_proxy.clone()).await;

        // Test getting supported & unsupported current notification values.
        send_get_notification(target_proxy.clone()).await;

        // Test setting player application settings.
        send_set_player_application_settings(target_proxy.clone()).await;

        // Test sending supported and unsupported panel commands.
        send_panel_commands(target_proxy.clone()).await;

        // Register a WatchNotification request.
        // The playback status has already changed, so this should trigger immediately.
        let res = target_proxy
            .watch_notification(
                NotificationEvent::PlaybackStatusChanged,
                Notification::new_empty(),
                0,
            )
            .await
            .expect("FIDL call should work");
        assert_eq!(
            Ok(Notification { status: Some(PlaybackStatus::Playing), ..Notification::new_empty() }),
            res
        );

        // The track_id hasn't changed, so this shouldn't resolve immediately. We need to send
        // a SessionUpdated event to change the state and therefore resolve the outstanding
        // notification.
        let mut watch = target_proxy.watch_notification(
            NotificationEvent::TrackChanged,
            Notification { track_id: Some(std::u64::MAX), ..Notification::new_empty() },
            0,
        );
        // This should not complete until we send the state update.
        assert_eq!(Poll::Pending, futures::poll!(&mut watch).map_err(|e| format!("{}", e)));

        // Update the state with a changed track_id.
        let delta1 = SessionInfoDelta {
            metadata: Some(create_metadata()),
            player_status: Some(create_player_status()),
            ..SessionInfoDelta::new_empty()
        };
        let res = watcher_client.session_updated(session1_id, delta1).await;
        assert_eq!(Ok(()), res.map_err(|e| format!("{}", e)));

        // We expect the `watch` future to have resolved now that an update has been received.
        let expected = Notification { track_id: Some(0), ..Notification::new_empty() };
        assert_eq!(
            Poll::Ready(Ok(Ok(expected))),
            futures::poll!(&mut watch).map_err(|e| format!("{}", e))
        );

        // Test the special case TrackPosChanged event.
        target_proxy
            .watch_notification(NotificationEvent::TrackPosChanged, Notification::new_empty(), 1)
            .await
    };

    pin_mut!(test_fut);
    // We expect the future to not complete yet because system time is currently fixed.
    let r0 = exec.run_until_stalled(&mut test_fut).map_err(|e| format!("{}", e));
    assert_eq!(Poll::Pending, r0);

    // Fast forward time by 10 seconds (555555555 + 1e10).
    exec.set_fake_time(fasync::Time::from_nanos(10555555555));
    exec.wake_expired_timers();
    let r1 = exec.run_until_stalled(&mut test_fut).map_err(|e| format!("{}", e));

    // The current track position is returned.
    // We expect the future to finish, now that the time has advanced by 10 seconds.
    let expected = Notification { pos: Some(55), ..Notification::new_empty() };
    assert_eq!(Poll::Ready(Ok(Ok(expected))), r1);

    Ok(())
}
