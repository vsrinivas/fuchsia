// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_avrcp::*,
    fidl_fuchsia_bluetooth_avrcp_test::*,
    fuchsia_async as fasync,
    futures::{
        self,
        future::{FutureExt, TryFutureExt},
        stream::StreamExt,
    },
    std::collections::VecDeque,
    tracing::warn,
};

use crate::{
    packets::PlaybackStatus as PacketPlaybackStatus,
    peer::{Controller, ControllerEvent as PeerControllerEvent},
    types::PeerError,
};

impl From<PeerError> for ControllerError {
    fn from(e: PeerError) -> Self {
        match e {
            PeerError::PacketError(_) => ControllerError::PacketEncoding,
            PeerError::AvctpError(_) => ControllerError::ProtocolError,
            PeerError::RemoteNotFound => ControllerError::RemoteNotConnected,
            PeerError::CommandNotSupported => ControllerError::CommandNotImplemented,
            PeerError::ConnectionFailure(_) => ControllerError::ConnectionError,
            PeerError::UnexpectedResponse => ControllerError::UnexpectedResponse,
            _ => ControllerError::UnknownFailure,
        }
    }
}

/// FIDL wrapper for a internal PeerController for control-related tasks.
pub struct ControllerService {
    /// Handle to internal controller client for the remote peer.
    controller: Controller,

    /// Incoming FIDL request stream from the FIDL client.
    fidl_stream: ControllerRequestStream,

    /// List of subscribed notifications the FIDL controller client cares about.
    notification_filter: Notifications,

    /// The current count of outgoing notifications currently outstanding an not acknowledged by the
    /// FIDL client.
    /// Used as part of flow control for delivery of notifications to the client.
    notification_window_counter: u32,

    /// Current queue of outstanding notifications not received by the client. Used as part of flow
    /// control.
    // At some point this may change where we consolidate outgoing events if the FIDL client
    // can't keep up and falls behind instead of keeping a queue.
    notification_queue: VecDeque<(i64, PeerControllerEvent)>,

    /// Notification state cache. Current interim state for the remote target peer. Sent to the
    /// controller FIDL client when they set their notification filter.
    notification_state: Notification,

    /// Notification state last update timestamp.
    notification_state_timestamp: i64,
}

impl ControllerService {
    const EVENT_WINDOW_LIMIT: u32 = 3;

    pub fn new(controller: Controller, fidl_stream: ControllerRequestStream) -> Self {
        Self {
            controller,
            fidl_stream,
            notification_filter: Notifications::empty(),
            notification_window_counter: 0,
            notification_queue: VecDeque::new(),
            notification_state: Notification::EMPTY,
            notification_state_timestamp: 0,
        }
    }

    async fn handle_fidl_request(&mut self, request: ControllerRequest) -> Result<(), Error> {
        match request {
            ControllerRequest::GetPlayerApplicationSettings { attribute_ids, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .get_player_application_settings(
                            attribute_ids.into_iter().map(Into::into).collect(),
                        )
                        .await
                        .map(Into::into)
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::SetPlayerApplicationSettings { requested_settings, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .set_player_application_settings(
                            crate::packets::PlayerApplicationSettings::from(&requested_settings),
                        )
                        .await
                        .map(Into::into)
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::GetMediaAttributes { responder } => {
                responder.send(
                    &mut self
                        .controller
                        .get_media_attributes()
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::GetPlayStatus { responder } => {
                responder.send(
                    &mut self.controller.get_play_status().await.map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::InformBatteryStatus { battery_status, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .inform_battery_status(battery_status)
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::SetNotificationFilter {
                notifications,
                // TODO(fxbug.dev/44332): coalesce position change intervals and notify on schedule
                position_change_interval: _,
                control_handle: _,
            } => {
                self.notification_filter = notifications;
                self.send_notification_cache()?;
            }
            ControllerRequest::NotifyNotificationHandled { control_handle: _ } => {
                debug_assert!(self.notification_window_counter != 0);
                self.notification_window_counter -= 1;
                if self.notification_window_counter < Self::EVENT_WINDOW_LIMIT {
                    match self.notification_queue.pop_front() {
                        Some((timestamp, event)) => {
                            self.handle_controller_event(timestamp, event)?;
                        }
                        None => {}
                    }
                }
            }
            ControllerRequest::SetAddressedPlayer { player_id: _, responder } => {
                responder.send(&mut Err(ControllerError::CommandNotImplemented))?;
            }
            ControllerRequest::SetAbsoluteVolume { requested_volume, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .set_absolute_volume(requested_volume)
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::SendCommand { command, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .send_keypress(command.into_primitive())
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
        };
        Ok(())
    }

    fn update_notification_from_controller_event(
        notification: &mut Notification,
        event: &PeerControllerEvent,
    ) {
        match event {
            PeerControllerEvent::PlaybackStatusChanged(playback_status) => {
                notification.status = Some(match playback_status {
                    PacketPlaybackStatus::Stopped => PlaybackStatus::Stopped,
                    PacketPlaybackStatus::Playing => PlaybackStatus::Playing,
                    PacketPlaybackStatus::Paused => PlaybackStatus::Paused,
                    PacketPlaybackStatus::FwdSeek => PlaybackStatus::FwdSeek,
                    PacketPlaybackStatus::RevSeek => PlaybackStatus::RevSeek,
                    PacketPlaybackStatus::Error => PlaybackStatus::Error,
                });
            }
            PeerControllerEvent::TrackIdChanged(track_id) => {
                notification.track_id = Some(*track_id);
            }
            PeerControllerEvent::PlaybackPosChanged(pos) => {
                notification.pos = Some(*pos);
            }
            PeerControllerEvent::VolumeChanged(volume) => {
                notification.volume = Some(*volume);
            }
            PeerControllerEvent::AvailablePlayersChanged => {
                notification.available_players_changed = Some(true)
            }
            PeerControllerEvent::AddressedPlayerChanged(player_id) => {
                notification.addressed_player = Some(*player_id);
            }
        }
    }

    fn handle_controller_event(
        &mut self,
        timestamp: i64,
        event: PeerControllerEvent,
    ) -> Result<(), Error> {
        self.notification_window_counter += 1;
        let control_handle: ControllerControlHandle = self.fidl_stream.control_handle();
        let mut notification = Notification::EMPTY;
        Self::update_notification_from_controller_event(&mut notification, &event);
        control_handle.send_on_notification(timestamp, notification).map_err(Error::from)
    }

    fn cache_controller_notification_state(&mut self, event: &PeerControllerEvent) {
        self.notification_state_timestamp = fuchsia_runtime::utc_time().into_nanos();
        Self::update_notification_from_controller_event(&mut self.notification_state, &event);
    }

    fn send_notification_cache(&mut self) -> Result<(), Error> {
        if self.notification_state_timestamp > 0 {
            let control_handle: ControllerControlHandle = self.fidl_stream.control_handle();

            let mut notification = Notification::EMPTY;

            if self.notification_filter.contains(Notifications::PLAYBACK_STATUS) {
                notification.status = self.notification_state.status;
            }

            if self.notification_filter.contains(Notifications::TRACK) {
                notification.track_id = self.notification_state.track_id;
            }

            if self.notification_filter.contains(Notifications::TRACK_POS) {
                notification.pos = self.notification_state.pos;
            }

            if self.notification_filter.contains(Notifications::VOLUME) {
                notification.volume = self.notification_state.volume;
            }

            if self.notification_filter.contains(Notifications::AVAILABLE_PLAYERS) {
                notification.available_players_changed =
                    self.notification_state.available_players_changed;
            }

            self.notification_window_counter += 1;
            return control_handle
                .send_on_notification(self.notification_state_timestamp, notification)
                .map_err(Error::from);
        }
        Ok(())
    }

    /// Returns true if the event should be dispatched.
    fn filter_controller_event(&self, event: &PeerControllerEvent) -> bool {
        match *event {
            PeerControllerEvent::PlaybackStatusChanged(_) => {
                self.notification_filter.contains(Notifications::PLAYBACK_STATUS)
            }
            PeerControllerEvent::TrackIdChanged(_) => {
                self.notification_filter.contains(Notifications::TRACK)
            }
            PeerControllerEvent::PlaybackPosChanged(_) => {
                self.notification_filter.contains(Notifications::TRACK_POS)
            }
            PeerControllerEvent::VolumeChanged(_) => {
                self.notification_filter.contains(Notifications::VOLUME)
            }
            PeerControllerEvent::AvailablePlayersChanged => {
                self.notification_filter.contains(Notifications::AVAILABLE_PLAYERS)
            }
            PeerControllerEvent::AddressedPlayerChanged(_) => {
                self.notification_filter.contains(Notifications::ADDRESSED_PLAYER)
            }
        }
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        let mut controller_events = self.controller.add_event_listener();
        loop {
            futures::select! {
                req = self.fidl_stream.select_next_some() => {
                    self.handle_fidl_request(req?).await?;
                }
                event = controller_events.select_next_some() => {
                    self.cache_controller_notification_state(&event);
                    if self.filter_controller_event(&event) {
                        let timestamp = fuchsia_runtime::utc_time().into_nanos();
                        if self.notification_window_counter > Self::EVENT_WINDOW_LIMIT {
                            self.notification_queue.push_back((timestamp, event));
                        } else {
                            self.handle_controller_event(timestamp, event)?;
                        }
                    }
                }
                complete => { return Ok(()); }
            }
        }
    }
}

/// FIDL wrapper for a internal PeerController for the test (ControllerExt) interface methods.
pub struct ControllerExtService {
    pub controller: Controller,
    pub fidl_stream: ControllerExtRequestStream,
}

impl ControllerExtService {
    async fn handle_fidl_request(&self, request: ControllerExtRequest) -> Result<(), Error> {
        match request {
            ControllerExtRequest::IsConnected { responder } => {
                responder.send(self.controller.is_control_connected())?;
            }
            ControllerExtRequest::GetEventsSupported { responder } => {
                match self.controller.get_supported_events().await {
                    Ok(events) => {
                        let mut r_events = vec![];
                        for e in events {
                            if let Some(target_event) =
                                NotificationEvent::from_primitive(u8::from(&e))
                            {
                                r_events.push(target_event);
                            }
                        }
                        responder.send(&mut Ok(r_events))?;
                    }
                    Err(peer_error) => {
                        responder.send(&mut Err(ControllerError::from(peer_error)))?
                    }
                }
            }
            ControllerExtRequest::SendRawVendorDependentCommand { pdu_id, command, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .send_raw_vendor_command(pdu_id, &command[..])
                        .map_err(ControllerError::from)
                        .await,
                )?;
            }
        };
        Ok(())
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        loop {
            futures::select! {
                req = self.fidl_stream.select_next_some() => {
                    self.handle_fidl_request(req?).await?;
                }
                complete => { return Ok(()); }
            }
        }
    }
}

/// Spawns a future that facilitates communication between a PeerController and a FIDL client.
pub fn spawn_service(
    controller: Controller,
    fidl_stream: ControllerRequestStream,
) -> fasync::Task<()> {
    fasync::Task::spawn(
        async move {
            let mut acc = ControllerService::new(controller, fidl_stream);
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: anyhow::Error| warn!("AVRCP client controller finished: {:?}", e)),
    )
}

/// Spawns a future that facilitates communication between a PeerController and a test FIDL client.
pub fn spawn_ext_service(
    controller: Controller,
    fidl_stream: ControllerExtRequestStream,
) -> fasync::Task<()> {
    fasync::Task::spawn(
        async move {
            let mut acc = ControllerExtService { controller, fidl_stream };
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: anyhow::Error| warn!("AVRCP test client controller finished: {:?}", e)),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::RemotePeerHandle;
    use crate::peer_manager::TargetDelegate;
    use async_test_helpers::run_while;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_bluetooth::types::PeerId;
    use pin_utils::pin_mut;
    use std::sync::Arc;

    fn setup() -> Controller {
        let (profile_proxy, _profile_requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("should have initialized");
        let peer = RemotePeerHandle::spawn_peer(
            PeerId(0x1),
            Arc::new(TargetDelegate::new()),
            profile_proxy,
        );

        Controller::new(peer)
    }

    #[fuchsia::test]
    /// Tests that the client stream handler will spawn a controller when a controller request
    /// successfully sets up a controller.
    fn run_client() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        // Set up for testing.
        let controller = setup();

        // Initialize client.
        let (proxy, server) =
            create_proxy_and_stream::<ControllerMarker>().expect("Controller proxy creation");
        let mut client = ControllerService::new(controller, server);
        let run_fut = client.run();
        pin_mut!(run_fut);

        // Verify that the client can process a request.
        let request_fut = proxy.set_addressed_player(1);
        let (_, mut run_fut) = run_while(&mut exec, run_fut, request_fut);

        // Verify that the client is still running.
        assert!(exec.run_until_stalled(&mut run_fut).is_pending());
    }

    /// Tests that the client stream handler will spawn a test controller when a
    /// controller request successfully sets up a controller.
    #[fuchsia::test]
    fn run_ext_client() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        // Set up testing.
        let controller = setup();

        // Initialize client.
        let (proxy, server) =
            create_proxy_and_stream::<ControllerExtMarker>().expect("Controller proxy creation");
        let mut client = ControllerExtService { controller, fidl_stream: server };

        let run_fut = client.run();
        pin_mut!(run_fut);

        // Verify that the client can process a request.
        let request_fut = proxy.is_connected();
        let (_, mut run_fut) = run_while(&mut exec, run_fut, request_fut);

        // Verify that the client is still running.
        assert!(exec.run_until_stalled(&mut run_fut).is_pending());
    }

    /// Tests that the notification object is updated based on the controller
    /// event value.
    #[fuchsia::test]
    fn update_notification_from_controller_event() {
        // Available players changed.
        let mut notification = Notification::EMPTY;
        let some_event = PeerControllerEvent::AvailablePlayersChanged;
        ControllerService::update_notification_from_controller_event(
            &mut notification,
            &some_event,
        );
        assert_eq!(
            notification,
            Notification { available_players_changed: Some(true), ..Notification::EMPTY }
        );

        // Addressed player changed.
        let mut notification = Notification::EMPTY;
        let some_event = PeerControllerEvent::AddressedPlayerChanged(4);
        ControllerService::update_notification_from_controller_event(
            &mut notification,
            &some_event,
        );
        assert_eq!(notification, Notification { addressed_player: Some(4), ..Notification::EMPTY });
    }

    /// Tests that controller events are filtered based on the notification
    /// filter set on the server.
    #[fuchsia::test]
    fn filter_controller_event() {
        let _exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        // Set up for testing.
        let controller = setup();

        // Initialize client.
        let (_proxy, server) =
            create_proxy_and_stream::<ControllerMarker>().expect("Controller proxy creation");
        let mut client = ControllerService::new(controller, server);

        // Set test filter.
        client.notification_filter = Notifications::PLAYBACK_STATUS
            | Notifications::TRACK
            | Notifications::CONNECTION
            | Notifications::AVAILABLE_PLAYERS;

        // Since notification filter includes available players changed event, should have returned true.
        // {
        let included_event = PeerControllerEvent::AvailablePlayersChanged;
        assert!(client.filter_controller_event(&included_event));
        // }

        // Since notification filter does not include volume changed event, should have returned false.
        let excluded_event = PeerControllerEvent::VolumeChanged(2);
        assert!(!client.filter_controller_event(&excluded_event));
    }
}
