// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl::encoding::Decodable,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_avrcp as avrcp, fidl_fuchsia_media as media,
    fidl_fuchsia_media_sessions2 as sessions2,
    fidl_table_validation::ValidFidlTable,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_syslog::{self, fx_log_info, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        channel::oneshot::{Receiver, Sender},
        select, FutureExt, StreamExt,
    },
};

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
#[fidl_table_src(sessions2::PlayerStatus)]
pub struct ValidPlayerStatus {
    #[fidl_field_type(optional)]
    pub duration: Option<i64>,
    pub player_state: sessions2::PlayerState,
    #[fidl_field_type(optional)]
    pub timeline_function: Option<media::TimelineFunction>,
    pub repeat_mode: sessions2::RepeatMode,
    pub shuffle_on: bool,
    pub content_type: sessions2::ContentType,
    #[fidl_field_type(optional)]
    pub error: Option<sessions2::Error>,
}

impl ValidPlayerStatus {
    /// Sets the `player_state` given a state from AVRCP.
    fn set_state_from_avrcp(&mut self, avrcp_status: avrcp::PlaybackStatus) {
        self.player_state = match avrcp_status {
            avrcp::PlaybackStatus::Stopped => sessions2::PlayerState::Idle,
            avrcp::PlaybackStatus::Playing => sessions2::PlayerState::Playing,
            avrcp::PlaybackStatus::Paused => sessions2::PlayerState::Paused,
            avrcp::PlaybackStatus::FwdSeek => sessions2::PlayerState::Playing,
            avrcp::PlaybackStatus::RevSeek => sessions2::PlayerState::Playing,
            avrcp::PlaybackStatus::Error => sessions2::PlayerState::Idle,
        };
    }

    /// Sets the TimelineFunction relating the current state and position given.
    /// Sets the function to None if there is no correlation.
    fn set_position(&mut self, position_millis: i64) {
        let subject_delta = match self.player_state {
            sessions2::PlayerState::Playing => 1,
            sessions2::PlayerState::Paused => 0,
            _ => {
                self.timeline_function = None;
                return;
            }
        };
        self.timeline_function = Some(media::TimelineFunction {
            subject_time: zx::Duration::from_millis(position_millis).into_nanos(),
            reference_time: fasync::Time::now().into_nanos(),
            subject_delta,
            reference_delta: 1,
        });
    }
}

pub(crate) struct AvrcpRelay {
    /// A sender that when sent will cause the relay task to stop. None if the task is not running.
    _stop: Option<Sender<()>>,
}

impl AvrcpRelay {
    /// Start a relay between AVRCP and MediaSession.
    /// A MediaSession is published with the information from the AVRCP target.
    /// This starts the relay.  The relay can be stopped by dropping it.
    pub(crate) fn start(peer_id: PeerId) -> Result<Self, Error> {
        let avrcp_svc = fuchsia_component::client::connect_to_service::<avrcp::PeerManagerMarker>()
            .context("Failed to connect to Bluetooth AVRCP interface")?;
        let session_svc =
            fuchsia_component::client::connect_to_service::<sessions2::PublisherMarker>()
                .context("Failed to connect to MediaSession interface")?;

        let (sender, receiver) = futures::channel::oneshot::channel();

        fasync::spawn(async move {
            if let Err(e) = Self::relay(avrcp_svc, session_svc, peer_id, receiver).await {
                fx_log_info!("Relay ended with {:?}", e);
            }
        });

        Ok(Self { _stop: Some(sender) })
    }

    async fn relay(
        mut avrcp: avrcp::PeerManagerProxy,
        publisher: sessions2::PublisherProxy,
        peer_id: PeerId,
        stop_signal: Receiver<()>,
    ) -> Result<(), Error> {
        let controller = connect_avrcp(&mut avrcp, peer_id).await?;

        let mut player_request_stream = connect_session_player(publisher).await?;

        let mut staged_info = Some(sessions2::PlayerInfoDelta {
            local: Some(true),
            player_capabilities: Some(sessions2::PlayerCapabilities {
                flags: Some(
                    sessions2::PlayerCapabilityFlags::Play
                        | sessions2::PlayerCapabilityFlags::Pause
                        | sessions2::PlayerCapabilityFlags::ChangeToNextItem
                        | sessions2::PlayerCapabilityFlags::ChangeToPrevItem,
                ),
                ..Decodable::new_empty()
            }),
            ..sessions2::PlayerInfoDelta::new_empty()
        });

        let mut hanging_watcher = None;

        let mut last_player_status = ValidPlayerStatus {
            player_state: sessions2::PlayerState::Idle,
            repeat_mode: sessions2::RepeatMode::Single,
            shuffle_on: false,
            content_type: sessions2::ContentType::Audio,
            duration: None,
            timeline_function: None,
            error: None,
        };

        // Get the initial Media Attributes and status
        let mut building = staged_info.as_mut().unwrap();
        // TODO(42914): sometimes these initially fail with "Protocol Error".  Avoid exiting the
        // relay for now.
        let _ = update_attributes(&controller, &mut building, &mut last_player_status).await;
        let _ = update_status(&controller, &mut last_player_status).await;
        building.player_status = Some(last_player_status.clone().into());

        let mut avrcp_notify_stream = controller.take_event_stream();
        let mut stop_signal = stop_signal.fuse();

        loop {
            let mut player_request_fut = player_request_stream.next();
            let mut avrcp_notify_fut = avrcp_notify_stream.next();

            select! {
                _ = stop_signal => {
                    fx_vlog!(1, "{} AVRCP relay stop: signal", peer_id);
                    break;
                }
                request = player_request_fut => {
                    if request.is_none() {
                        fx_vlog!(1, "Player request stream is closed, quitting AVRCP.");
                        break;
                    }
                    match request.unwrap()? {
                        sessions2::PlayerRequest::WatchInfoChange { responder } => {
                            if let Some(_) = hanging_watcher.take() {
                                return Err(format_err!("Concurrent watches issued: not allowed"));
                            }
                            hanging_watcher = Some(responder);
                        }
                        sessions2::PlayerRequest::Pause { .. } => {
                            let _ = controller.send_command(avrcp::AvcPanelCommand::Pause).await;
                        },
                        sessions2::PlayerRequest::Play { .. } => {
                            let _ = controller.send_command(avrcp::AvcPanelCommand::Play).await;
                        },
                        sessions2::PlayerRequest::NextItem { .. } => {
                            let _ = controller.send_command(avrcp::AvcPanelCommand::Forward).await;
                        },
                        sessions2::PlayerRequest::PrevItem { .. } => {
                            let _ = controller.send_command(avrcp::AvcPanelCommand::Backward).await;
                        },
                        x => fx_log_info!("Unhandled request from Player: {:?}", x),
                    }
                }
                event = avrcp_notify_fut => {
                    if event.is_none() {
                        fx_log_info!("{} AVRCP relay stop: notification stream gone", peer_id);
                        break;
                    }
                    let avrcp::ControllerEvent::OnNotification { timestamp, notification } = event.unwrap()?;
                    fx_vlog!(1, "Got Notification from AVRCP: {:?}", notification);

                    let mut player_status_updated = false;

                    if let Some(millis) = notification.pos {
                        last_player_status.set_position(millis as i64);
                        player_status_updated = true;
                    }

                    if notification.status.is_some() || notification.track_id.is_some() {
                        let mut building = staged_info.get_or_insert_with(sessions2::PlayerInfoDelta::new_empty);
                        update_attributes(&controller, &mut building, &mut last_player_status).await?;
                        player_status_updated = true;

                    }

                    if notification.status.is_some() {
                        update_status(&controller, &mut last_player_status).await?;
                        player_status_updated = true;
                    }

                    if player_status_updated {
                        let building = staged_info.get_or_insert_with(sessions2::PlayerInfoDelta::new_empty);
                        building.player_status = Some(last_player_status.clone().into());
                    }

                    // Notify that the notification is handled so we can receive another one.
                    let _ = controller.notify_notification_handled()?;
                }
                complete => unreachable!(),
            }

            if staged_info.is_some() && hanging_watcher.is_some() {
                fx_log_info!("Sending watcher info: {:?}", staged_info.as_ref().unwrap());
                hanging_watcher.take().unwrap().send(staged_info.take().unwrap())?;
            }
        }
        Ok(())
    }
}

async fn update_attributes(
    controller: &avrcp::ControllerProxy,
    info_delta: &mut sessions2::PlayerInfoDelta,
    status: &mut ValidPlayerStatus,
) -> Result<(), Error> {
    let attributes = controller
        .get_media_attributes()
        .await?
        .or_else(|e| Err(format_err!("AVRCP error: {:?}", e)))?;
    info_delta.metadata = Some(attributes_to_metadata(&attributes));

    if !attributes.playing_time.is_empty() {
        if let Ok(millis) = attributes.playing_time.parse::<i64>() {
            status.duration = Some(zx::Duration::from_millis(millis).into_nanos());
        }
    }
    Ok(())
}

async fn update_status(
    controller: &avrcp::ControllerProxy,
    status: &mut ValidPlayerStatus,
) -> Result<(), Error> {
    let avrcp_status = controller
        .get_play_status()
        .await?
        .or_else(|e| Err(format_err!("AVRCP error: {:?}", e)))?;
    let playback_status =
        avrcp_status.playback_status.ok_or(format_err!("PlayStatus must have playback status"))?;
    status.set_state_from_avrcp(playback_status);
    status.duration =
        avrcp_status.song_length.map(|m| zx::Duration::from_millis(m as i64).into_nanos());
    avrcp_status.song_position.map(|m| status.set_position(m as i64));
    Ok(())
}

macro_rules! nonempty_to_property {
    ( $source:expr, $prop_str:expr, $target:ident ) => {
        if !($source).is_empty() {
            $target.push(media::Property { label: $prop_str.to_string(), value: $source.clone() });
        }
    };
}

fn attributes_to_metadata(attributes: &avrcp::MediaAttributes) -> media::Metadata {
    let mut properties = Vec::new();
    nonempty_to_property!(attributes.title, media::METADATA_LABEL_TITLE, properties);
    nonempty_to_property!(attributes.artist_name, media::METADATA_LABEL_ARTIST, properties);
    nonempty_to_property!(attributes.album_name, media::METADATA_LABEL_ALBUM, properties);
    nonempty_to_property!(attributes.track_number, media::METADATA_LABEL_TRACK_NUMBER, properties);
    nonempty_to_property!(attributes.total_number_of_tracks, "total_number_of_tracks", properties);
    nonempty_to_property!(attributes.genre, media::METADATA_LABEL_GENRE, properties);
    media::Metadata { properties }
}

async fn connect_avrcp(
    avrcp: &mut avrcp::PeerManagerProxy,
    peer_id: PeerId,
) -> Result<avrcp::ControllerProxy, Error> {
    let (controller, server) = endpoints::create_proxy()?;

    let _ = avrcp.get_controller_for_target(&peer_id.to_string(), server).await?;

    controller.set_notification_filter(
        avrcp::Notifications::PlaybackStatus
            | avrcp::Notifications::Track
            | avrcp::Notifications::TrackPos
            | avrcp::Notifications::Connection,
        5,
    )?;

    Ok(controller)
}

async fn connect_session_player(
    publisher: sessions2::PublisherProxy,
) -> Result<sessions2::PlayerRequestStream, Error> {
    let (player_client, player_request_stream) = endpoints::create_request_stream()?;

    let registration =
        sessions2::PlayerRegistration { domain: Some("some_domain_string".to_string()) };

    publisher.publish(player_client, registration).await.context("publishing player")?;

    Ok(player_request_stream)
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::{self, RequestStream};
    use fuchsia_async::pin_mut;
    use futures::{task::Poll, Future};
    use std::{convert::TryInto, pin::Pin};

    fn setup_publisher_proxy(
    ) -> Result<(sessions2::PublisherProxy, sessions2::PublisherRequestStream), fidl::Error> {
        endpoints::create_proxy_and_stream::<sessions2::PublisherMarker>()
    }

    fn setup_avrcp_proxy(
    ) -> Result<(avrcp::PeerManagerProxy, avrcp::PeerManagerRequestStream), fidl::Error> {
        endpoints::create_proxy_and_stream::<avrcp::PeerManagerMarker>()
    }

    fn expect_media_attributes_request(
        exec: &mut fasync::Executor,
        controller_requests: &mut avrcp::ControllerRequestStream,
    ) -> Result<(), fidl::Error> {
        // Should ask for the current media info and the status to return the correct results.
        match exec.run_until_stalled(&mut controller_requests.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::GetMediaAttributes { responder }))) => {
                responder.send(&mut Ok(avrcp::MediaAttributes {
                    title: "Might Be Right".to_string(),
                    artist_name: "White Reaper".to_string(),
                    album_name: "You Deserve Love".to_string(),
                    track_number: "7".to_string(),
                    total_number_of_tracks: "10".to_string(),
                    genre: "Alternative".to_string(),
                    playing_time: "237000".to_string(),
                }))
            }
            x => panic!("Expected a GetMediaAttributes request, got {:?}", x),
        }
    }

    fn expect_play_status_request(
        exec: &mut fasync::Executor,
        controller_requests: &mut avrcp::ControllerRequestStream,
    ) -> Result<(), fidl::Error> {
        match exec.run_until_stalled(&mut controller_requests.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::GetPlayStatus { responder }))) => {
                responder.send(&mut Ok(avrcp::PlayStatus {
                    song_length: Some(237000),
                    song_position: Some(1000),
                    playback_status: Some(avrcp::PlaybackStatus::Playing),
                }))
            }
            x => panic!("Expected a GetPlayStatus request, got {:?}", x),
        }
    }

    fn finish_relay_setup(
        mut relay_fut: &mut Pin<&mut impl Future>,
        mut exec: &mut fasync::Executor,
        mut publisher_request_stream: sessions2::PublisherRequestStream,
        mut avrcp_request_stream: avrcp::PeerManagerRequestStream,
    ) -> Result<(sessions2::PlayerProxy, avrcp::ControllerRequestStream), Error> {
        // Connects to AVRCP first.
        let complete = exec.run_until_stalled(&mut avrcp_request_stream.select_next_some());
        let (mut controller_request_stream, responder) = match complete {
            Poll::Ready(Ok(avrcp::PeerManagerRequest::GetControllerForTarget {
                client,
                responder,
                ..
            })) => (client.into_stream()?, responder),
            x => panic!("Expected a GetController request, got {:?}", x),
        };

        responder.send(&mut Ok(()))?;

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let complete = exec.run_until_stalled(&mut controller_request_stream.select_next_some());
        match complete {
            Poll::Ready(Ok(avrcp::ControllerRequest::SetNotificationFilter { .. })) => {}
            x => panic!("Expected notifications to be set, got {:?}", x),
        };

        let complete = exec.run_until_stalled(&mut publisher_request_stream.select_next_some());
        let player_client = match complete {
            Poll::Ready(Ok(sessions2::PublisherRequest::Publish { player, responder, .. })) => {
                responder.send(1).expect("should have been able to send session response");
                player.into_proxy()?
            }
            x => panic!("Expected PublishPlayer, got {:?}", x),
        };

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        expect_media_attributes_request(&mut exec, &mut controller_request_stream)?;
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        expect_play_status_request(&mut exec, &mut controller_request_stream)?;
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // At this point, the relay is setup and should be waiting for requests from each of
        // player_client and sending commands / getting notifications from avrcp.
        Ok((player_client, controller_request_stream))
    }

    #[test]
    /// Test that the relay sets up the connection to AVRCP and Sessions and stops on the stop
    /// signal.
    fn test_relay_setup() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (player_client, mut controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        // Sending a stop should drop all the things and the future should complete.
        stop_sender.send(()).expect("should be able to send a stop");

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_ready());

        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(None) => {}
            x => panic!("Expected controller to be dropped, but got {:?}", x),
        };

        let mut watch_info_fut = player_client.watch_info_change();
        match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Err(_e)) => {}
            x => panic!("Expected player to be disconnected, but got {:?} from watch_info", x),
        };
        Ok(())
    }

    #[test]
    /// Relay will stop when AVRCP closes the notification channel.
    fn test_relay_avrcp_ends() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (player_client, controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        // Closing the AVRCP controller should end the relay.
        drop(controller_request_stream);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_ready());

        // The MediaSession should also drop based on this.
        let mut watch_info_fut = player_client.watch_info_change();
        match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Err(_e)) => {}
            x => panic!("Expected player to be disconnected, but got {:?} from watch_info", x),
        };
        Ok(())
    }

    #[test]
    /// Relay will stop when Player stops asking for updates.
    fn test_relay_player_ends() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (player_client, mut controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        // Closing of the MediaSession should end the relay.
        drop(player_client);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_ready());

        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(None) => {}
            x => panic!("Expected controller to be dropped, but got {:?}", x),
        };
        Ok(())
    }

    #[test]
    /// When mediasession initially asks for media info, a query of the remote AVRCP is made and
    /// the data is translated.
    fn test_relay_sends_correct_media_info() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(7000));

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        let (player_client, _controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        let mut watch_info_fut = player_client.watch_info_change();

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // Should return to the player the initial data.
        let info_delta = match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Ok(delta)) => delta,
            x => panic!("Expected WatchInfoChange to complete, instead: {:?}", x),
        };

        assert_eq!(
            ValidPlayerStatus {
                duration: Some(237000_000_000),
                player_state: sessions2::PlayerState::Playing,
                timeline_function: Some(media::TimelineFunction {
                    subject_time: 1000_000_000,
                    reference_time: 7000,
                    subject_delta: 1,
                    reference_delta: 1,
                }),
                repeat_mode: sessions2::RepeatMode::Single,
                shuffle_on: false,
                content_type: sessions2::ContentType::Audio,
                error: None
            },
            info_delta.player_status.unwrap().try_into().expect("valid player status")
        );

        Ok(())
    }

    #[test]
    /// When playback status changes the new track info is sent to the Player client.
    fn test_relay_new_avrcp_track_info() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(7000));

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        let (player_client, mut controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        let mut watch_info_fut = player_client.watch_info_change();

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // Should return to the player the initial data.
        let _info_delta = match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Ok(delta)) => delta,
            x => panic!("Expected WatchInfoChange to complete, instead: {:?}", x),
        };

        // Queueing up another one with no change should just hang.
        let mut watch_info_fut = player_client.watch_info_change();

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        assert!(exec.run_until_stalled(&mut watch_info_fut).is_pending());

        // When a play status change notification happens, we get new requests.
        controller_request_stream.control_handle().send_on_notification(
            7000,
            avrcp::Notification {
                status: Some(avrcp::PlaybackStatus::Paused),
                ..avrcp::Notification::new_empty()
            },
        )?;

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // Should ask for the current media info and the status to return the correct results.
        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::GetMediaAttributes { responder }))) => {
                responder.send(&mut Ok(avrcp::MediaAttributes {
                    title: "Moneygrabber".to_string(),
                    artist_name: "Fitz and the Tantrums".to_string(),
                    album_name: "Pickin' Up the Pieces".to_string(),
                    track_number: "4".to_string(),
                    total_number_of_tracks: "11".to_string(),
                    genre: "Alternative".to_string(),
                    playing_time: "189000".to_string(),
                }))?;
            }
            x => panic!("Expected a GetMediaAttributes request, got {:?}", x),
        }
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::GetPlayStatus { responder }))) => {
                responder.send(&mut Ok(avrcp::PlayStatus {
                    song_length: Some(189000),
                    song_position: Some(1000),
                    playback_status: Some(avrcp::PlaybackStatus::Paused),
                }))?;
            }
            x => panic!("Expected a GetPlayStatus request, got {:?}", x),
        };
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // After the AVRCP requests, the info should have the delta.
        let info_delta = match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Ok(delta)) => delta,
            x => panic!("Expected WatchInfoChange to complete, instead: {:?}", x),
        };

        // After the notification is handled we should get an ack.
        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::NotifyNotificationHandled {
                ..
            }))) => {}
            x => panic!("Expected ack of notification, but got {:?}", x),
        };

        assert_eq!(
            ValidPlayerStatus {
                duration: Some(189000_000_000),
                player_state: sessions2::PlayerState::Paused,
                timeline_function: Some(media::TimelineFunction {
                    subject_time: 1000_000_000,
                    reference_time: 7000,
                    subject_delta: 0,
                    reference_delta: 1,
                }),
                repeat_mode: sessions2::RepeatMode::Single,
                shuffle_on: false,
                content_type: sessions2::ContentType::Audio,
                error: None
            },
            info_delta.player_status.unwrap().try_into().expect("valid player status")
        );

        Ok(())
    }

    #[test]
    /// When the position update happens, the new position is updated for the Player.
    fn test_relay_updates_position() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(7000));

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        let (player_client, mut controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        let mut watch_info_fut = player_client.watch_info_change();

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // Should return to the player the initial data.
        let _info_delta = match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Ok(delta)) => delta,
            x => panic!("Expected WatchInfoChange to complete, instead: {:?}", x),
        };

        // Queueing up another one with no change should just hang.
        let mut watch_info_fut = player_client.watch_info_change();

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        assert!(exec.run_until_stalled(&mut watch_info_fut).is_pending());

        exec.set_fake_time(fasync::Time::from_nanos(9000));

        // When a play status change notification happens, we get new requests.
        controller_request_stream.control_handle().send_on_notification(
            9000,
            avrcp::Notification { pos: Some(3051), ..avrcp::Notification::new_empty() },
        )?;

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // After the notification is handled we should get an ack.
        match exec.run_until_stalled(&mut controller_request_stream.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::NotifyNotificationHandled {
                ..
            }))) => {}
            x => panic!("Expected ack of notification, but got {:?}", x),
        };

        // The info should have the delta.
        let info_delta = match exec.run_until_stalled(&mut watch_info_fut) {
            Poll::Ready(Ok(delta)) => delta,
            x => panic!("Expected WatchInfoChange to complete, instead: {:?}", x),
        };

        assert_eq!(
            ValidPlayerStatus {
                duration: Some(237000_000_000),
                player_state: sessions2::PlayerState::Playing,
                timeline_function: Some(media::TimelineFunction {
                    subject_time: 3051_000_000,
                    reference_time: 9000,
                    subject_delta: 1,
                    reference_delta: 1,
                }),
                repeat_mode: sessions2::RepeatMode::Single,
                shuffle_on: false,
                content_type: sessions2::ContentType::Audio,
                error: None
            },
            info_delta.player_status.unwrap().try_into().expect("valid player status")
        );

        Ok(())
    }

    fn expect_panel_command(
        exec: &mut fasync::Executor,
        controller_requests: &mut avrcp::ControllerRequestStream,
        expected_command: avrcp::AvcPanelCommand,
    ) -> Result<(), fidl::Error> {
        match exec.run_until_stalled(&mut controller_requests.next()) {
            Poll::Ready(Some(Ok(avrcp::ControllerRequest::SendCommand { command, responder }))) => {
                assert_eq!(expected_command, command);
                responder.send(&mut Ok(()))
            }
            x => panic!("Expected a SendCommand({:?}) request, got {:?}", expected_command, x),
        }
    }

    #[test]
    /// When commands come from the Player, they are relayed to the AVRCP commands.
    fn test_relay_sends_commands() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");

        let (publisher_proxy, publisher_request_stream) = setup_publisher_proxy()?;
        let (avrcp_proxy, avrcp_request_stream) = setup_avrcp_proxy()?;

        let (_stop_sender, receiver) = futures::channel::oneshot::channel();

        let peer_id = PeerId(0);

        let relay_fut = AvrcpRelay::relay(avrcp_proxy, publisher_proxy, peer_id, receiver);

        pin_mut!(relay_fut);

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        let (player_client, mut controller_request_stream) = finish_relay_setup(
            &mut relay_fut,
            &mut exec,
            publisher_request_stream,
            avrcp_request_stream,
        )?;

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        player_client.pause()?;
        player_client.play()?;
        player_client.next_item()?;
        player_client.prev_item()?;

        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        expect_panel_command(
            &mut exec,
            &mut controller_request_stream,
            avrcp::AvcPanelCommand::Pause,
        )?;
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        expect_panel_command(
            &mut exec,
            &mut controller_request_stream,
            avrcp::AvcPanelCommand::Play,
        )?;
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        expect_panel_command(
            &mut exec,
            &mut controller_request_stream,
            avrcp::AvcPanelCommand::Forward,
        )?;
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        expect_panel_command(
            &mut exec,
            &mut controller_request_stream,
            avrcp::AvcPanelCommand::Backward,
        )?;

        Ok(())
    }
}
