// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl::endpoints::{create_proxy, create_request_stream};
use fidl_fuchsia_bluetooth_avrcp as fidl_avrcp;
use fidl_fuchsia_media_sessions2::{
    DiscoveryMarker, DiscoveryProxy, SessionControlProxy, SessionInfoDelta, SessionsWatcherRequest,
    SessionsWatcherRequestStream, WatchOptions,
};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon::DurationNum;
use futures::{future, Future, TryStreamExt};
use parking_lot::RwLock;
use std::{collections::HashMap, sync::Arc};
use tracing::{trace, warn};

use crate::media::{
    media_state::{MediaState, MEDIA_SESSION_ADDRESSED_PLAYER_ID, MEDIA_SESSION_DISPLAYABLE_NAME},
    media_types::Notification,
};
use crate::types::{
    bounded_queue::BoundedQueue, NotificationData, MAX_NOTIFICATION_EVENT_QUEUE_SIZE,
};

/// The system-wide ID of a MediaSession, as created and assigned by the media system.
/// These IDs are used internally to disambiguate media sessions.
#[derive(Debug, Clone, Copy, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub(crate) struct MediaSessionId(pub u64);

#[derive(Debug, Clone)]
pub(crate) struct MediaSessions {
    inner: Arc<RwLock<MediaSessionsInner>>,
}

impl MediaSessions {
    pub fn create() -> Self {
        Self { inner: Arc::new(RwLock::new(MediaSessionsInner::new())) }
    }

    // Returns a future that watches local MediaSessions for updates.
    pub fn watch(&self) -> impl Future<Output = Result<(), anyhow::Error>> + '_ {
        // MediaSession Service Setup
        // Set up the MediaSession Discovery service. Connect to the session watcher.
        let discovery = connect_to_protocol::<DiscoveryMarker>()
            .expect("Couldn't connect to discovery service.");
        let (watcher_client, watcher_requests) =
            create_request_stream().expect("Error creating watcher request stream");

        // Subscribe to all players. The active player is the player that has sent this
        // component the most recent SessionUpdate with an active status.
        let watch_options = WatchOptions::EMPTY;

        if let Err(e) = discovery
            .watch_sessions(watch_options, watcher_client)
            .context("Should be able to watch media sessions")
        {
            warn!("FIDL error watching sessions: {:?}", e);
        }
        // End MediaSession Service Setup

        self.watch_media_sessions(discovery, watcher_requests)
    }

    pub fn get_active_session_id(&self) -> Option<MediaSessionId> {
        self.inner.read().active_session_id
    }

    pub fn get_active_session(&self) -> Result<MediaState, Error> {
        let r_inner = self.inner.read().get_active_session();
        r_inner.ok_or(format_err!("No active player"))
    }

    pub fn get_supported_notification_events(&self) -> Vec<fidl_avrcp::NotificationEvent> {
        self.inner.read().get_supported_notification_events()
    }

    pub fn get_media_player_items(
        &self,
    ) -> Result<Vec<fidl_avrcp::MediaPlayerItem>, fidl_avrcp::TargetAvcError> {
        self.inner.read().get_media_player_items()
    }

    pub fn set_addressed_player(
        &self,
        player_id: fidl_avrcp::AddressedPlayerId,
    ) -> Result<(), fidl_avrcp::TargetAvcError> {
        self.inner.write().set_addressed_player(player_id)
    }

    pub fn update_battery_status(&self, status: fidl_avrcp::BatteryStatus) {
        self.inner.write().update_battery_status(status);
    }

    /// Registers a notification identified by the `event_id`.
    pub fn register_notification(
        &self,
        event_id: fidl_avrcp::NotificationEvent,
        current: Notification,
        pos_change_interval: u32,
        responder: fidl_avrcp::TargetHandlerWatchNotificationResponder,
    ) -> Result<(), fidl::Error> {
        let timeout = {
            let mut write = self.inner.write();
            write.register_notification(event_id, current, pos_change_interval, responder)?
        };

        // If the `register_notification` call returned a timeout, spawn a task to update any
        // outstanding notifications at the deadline.
        if let Some(deadline) = timeout {
            let media_sessions = self.clone();
            let update_fut = future::pending().on_timeout(deadline, move || {
                media_sessions.inner.write().update_notification_responders();
            });
            fasync::Task::spawn(update_fut).detach();
        }
        Ok(())
    }

    pub(crate) async fn watch_media_sessions(
        &self,
        discovery: DiscoveryProxy,
        mut watcher_requests: SessionsWatcherRequestStream,
    ) -> Result<(), anyhow::Error> {
        let sessions_inner = self.inner.clone();
        while let Some(req) =
            watcher_requests.try_next().await.expect("Failed to serve Watcher service")
        {
            match req {
                SessionsWatcherRequest::SessionUpdated {
                    session_id: id,
                    session_info_delta: delta,
                    responder,
                } => {
                    responder.send()?;
                    trace!("MediaSession update: id[{}], delta[{:?}]", id, delta);

                    // Since we are listening to all sessions, update the currently
                    // active media session id every time a watcher event is triggered and
                    // the session is locally active.
                    // If there is currently no active session, default the current session
                    // to be the active session.
                    // This means AVRCP commands will be queried/set to the player that has most
                    // recently changed to active status.
                    match (self.get_active_session_id(), delta.is_locally_active) {
                        (None, _) | (_, Some(true)) => {
                            let _ = sessions_inner
                                .write()
                                .update_target_session_id(Some(MediaSessionId(id)));
                        }
                        _ => {}
                    };

                    // If this is our first time receiving updates from this MediaPlayer, create
                    // a session control proxy and connect to the session.
                    sessions_inner.write().create_or_update_session(
                        discovery.clone(),
                        MediaSessionId(id),
                        delta,
                        &create_session_control_proxy,
                    )?;

                    trace!("MediaSession state after update: state[{:?}]", sessions_inner);
                }
                SessionsWatcherRequest::SessionRemoved { session_id, responder } => {
                    // A media session with id `session_id` has been removed.
                    responder.send()?;

                    // Clear any outstanding notifications with a player changed response.
                    // Clear the currently active session, if it equals `session_id`.
                    // Clear entry in state map.
                    let _ = sessions_inner.write().clear_session(&MediaSessionId(session_id));
                    trace!(
                        "Removed session [{:?}] from state map: {:?}",
                        session_id,
                        sessions_inner
                    );
                }
            }
        }
        Ok(())
    }
}

#[derive(Debug)]
pub(crate) struct MediaSessionsInner {
    // The currently active MediaSession id.
    // If present, the `active_session_id` should be present in `map`.
    active_session_id: Option<MediaSessionId>,
    // The map of ids to the respective media session.
    map: HashMap<MediaSessionId, MediaState>,
    // The map of outstanding notifications.
    notifications: HashMap<fidl_avrcp::NotificationEvent, BoundedQueue<NotificationData>>,
}

impl MediaSessionsInner {
    pub fn new() -> Self {
        Self { active_session_id: None, map: HashMap::new(), notifications: HashMap::new() }
    }

    pub fn get_active_session(&self) -> Option<MediaState> {
        self.active_session_id.as_ref().and_then(|id| self.map.get(id).cloned())
    }

    pub fn get_supported_notification_events(&self) -> Vec<fidl_avrcp::NotificationEvent> {
        vec![
            fidl_avrcp::NotificationEvent::AddressedPlayerChanged,
            fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
            fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
            fidl_avrcp::NotificationEvent::TrackChanged,
            fidl_avrcp::NotificationEvent::TrackPosChanged,
            fidl_avrcp::NotificationEvent::BattStatusChanged,
        ]
    }

    /// Returns the list of supported Media Player Items.
    pub fn get_media_player_items(
        &self,
    ) -> Result<Vec<fidl_avrcp::MediaPlayerItem>, fidl_avrcp::TargetAvcError> {
        // Return a fixed singleton item as we currently only support one active media session.
        self.get_active_session().map_or(
            Err(fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers),
            |state| {
                Ok(vec![fidl_avrcp::MediaPlayerItem {
                    player_id: Some(MEDIA_SESSION_ADDRESSED_PLAYER_ID),
                    major_type: Some(fidl_avrcp::MajorPlayerType::AUDIO),
                    sub_type: Some(fidl_avrcp::PlayerSubType::empty()),
                    playback_status: Some(
                        state.session_info().get_play_status().get_playback_status(),
                    ),
                    displayable_name: Some(MEDIA_SESSION_DISPLAYABLE_NAME.to_string()),
                    ..fidl_avrcp::MediaPlayerItem::EMPTY
                }])
            },
        )
    }

    /// Removes the tracked MediaSession specified by `id`.
    /// Returns the state associated with the removed session.
    pub fn clear_session(&mut self, id: &MediaSessionId) -> Option<MediaState> {
        if Some(id) == self.active_session_id.as_ref() {
            let _ = self.update_target_session_id(None);
        }
        self.map.remove(id)
    }

    /// Clears all outstanding notifications with an AddressedPlayerChanged error.
    pub fn clear_notification_responders(&mut self) {
        for notif_data in self.notifications.drain().map(|(_, q)| q.into_iter()).flatten() {
            if let Err(e) = notif_data.update_responder(
                &fidl_avrcp::NotificationEvent::TrackChanged, // Irrelevant Event ID.
                Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged),
            ) {
                warn!("There was an error clearing the responder: {:?}", e);
            }
        }
        trace!("After evicting cleared responders: {:?}", self.notifications);
    }

    /// Updates the active target session ID with the new session specified by `id`.
    /// Clear all outstanding notifications if the session has changed.
    /// Return the previous ID if the active session has changed.
    pub fn update_target_session_id(
        &mut self,
        id: Option<MediaSessionId>,
    ) -> Option<MediaSessionId> {
        if id == self.active_session_id {
            return None;
        }

        self.clear_notification_responders();
        std::mem::replace(&mut self.active_session_id, id)
    }

    /// Update outstanding notification responders for the active session.
    pub fn update_notification_responders(&mut self) {
        let state = if let Some(state) = self.get_active_session() {
            state.clone()
        } else {
            return;
        };
        let curr_value: Notification = state.session_info().into();

        self.notifications = self
            .notifications
            .drain()
            .map(|(event_id, queue)| {
                (
                    event_id,
                    queue
                        .into_iter()
                        .filter_map(|notif_data| {
                            notif_data
                                .update_responder(&event_id, Ok(curr_value.clone()))
                                .unwrap_or(None)
                        })
                        .collect(),
                )
            })
            .collect();

        trace!("Notifications after evicting updated responders: {:?}", self.notifications);
    }

    /// Applies a MediaSession update `delta` for the provided session `id`.
    pub fn create_or_update_session<F>(
        &mut self,
        discovery: DiscoveryProxy,
        id: MediaSessionId,
        delta: SessionInfoDelta,
        create_fn: F,
    ) -> Result<(), Error>
    where
        F: Fn(DiscoveryProxy, MediaSessionId) -> Result<SessionControlProxy, Error>,
    {
        self.map
            .entry(id)
            .or_insert({
                let session_proxy = create_fn(discovery, id)?;
                MediaState::new(session_proxy)
            })
            .update_session_info(delta);

        // Update any outstanding notification responders with the change in state.
        self.update_notification_responders();
        Ok(())
    }

    /// Registers a notification watcher for the provided `event_id`.
    /// Returns an optional notification response deadline.
    pub fn register_notification(
        &mut self,
        event_id: fidl_avrcp::NotificationEvent,
        current: Notification,
        pos_change_interval: u32,
        responder: fidl_avrcp::TargetHandlerWatchNotificationResponder,
    ) -> Result<Option<fasync::Time>, fidl::Error> {
        // If the `event_id` is not supported, reject the registration.
        if !self.get_supported_notification_events().contains(&event_id) {
            responder.send(&mut Err(fidl_avrcp::TargetAvcError::RejectedInvalidParameter))?;
            return Ok(None);
        }

        // If there is no current media session, reject the registration.
        //
        // For the TrackPosChanged event id, ignore the input parameter `current`
        // and use current local values.
        // The response timeout (nanos) will be the scaled (nonezero) `pos_change_interval`
        // duration after the current time.
        //
        // For AddressedPlayerChanged, send an immediate reject because we currently only
        // support one player.
        //
        // For all other event_ids, use the provided `current` parameter, and the `response_timeout`
        // is not needed.
        let active_session = match self.get_active_session() {
            None => {
                responder.send(&mut Err(fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers))?;
                return Ok(None);
            }
            Some(session) => session,
        };
        let (current_values, response_timeout) = match (event_id, pos_change_interval) {
            (fidl_avrcp::NotificationEvent::TrackPosChanged, 0) => {
                responder.send(&mut Err(fidl_avrcp::TargetAvcError::RejectedInvalidParameter))?;
                return Ok(None);
            }
            (fidl_avrcp::NotificationEvent::TrackPosChanged, interval) => {
                let current_values = active_session.session_info().into();
                let response_timeout = active_session
                    .session_info()
                    .get_playback_rate()
                    .reference_deadline((interval as i64).seconds());
                (current_values, response_timeout)
            }
            (fidl_avrcp::NotificationEvent::AddressedPlayerChanged, _) => {
                responder
                    .send(&mut Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged))?;
                return Ok(None);
            }
            (_, _) => (current, None),
        };

        // Given the response timeout (nanos), the deadline is the `response_timeout`
        // amount of time after now.
        let response_deadline = response_timeout.map(|t| fasync::Time::after(t));

        let data = NotificationData::new(
            event_id,
            current_values,
            pos_change_interval,
            response_deadline,
            responder,
        );

        let _evicted = self
            .notifications
            .entry(event_id)
            .or_insert(BoundedQueue::new(MAX_NOTIFICATION_EVENT_QUEUE_SIZE))
            .insert(data);

        // Notify the evicted responder that the TG has removed it from the active list of responders.
        // Reply with the current value of the notification.
        // This will happen automatically, when `_evicted` is dropped.

        // Update outstanding responders with potentially new session data.
        self.update_notification_responders();

        Ok(response_deadline)
    }

    /// Sets the active addressed player to the provided `player_id`.
    ///
    /// Returns `RejectedNoAvailablePlayers` if there is no active session.
    /// Returns `RejectedInvalidPlayerId` if the provided `player_id` does not match the fixed
    /// `MEDIA_SESSION_ADDRESSED_PLAYER_ID` as this implementation currently only supports one
    /// player.
    fn set_addressed_player(
        &self,
        player_id: fidl_avrcp::AddressedPlayerId,
    ) -> Result<(), fidl_avrcp::TargetAvcError> {
        self.get_active_session().map_or(
            Err(fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers),
            |_| {
                if player_id.id == MEDIA_SESSION_ADDRESSED_PLAYER_ID {
                    Ok(())
                } else {
                    Err(fidl_avrcp::TargetAvcError::RejectedInvalidPlayerId)
                }
            },
        )
    }

    /// Updates all sessions with the Fuchsia-local battery `status`.
    fn update_battery_status(&mut self, status: fidl_avrcp::BatteryStatus) {
        for (_session_id, state) in self.map.iter_mut() {
            state.update_battery_status(status);
        }

        // Update any outstanding notification responders for the active session as the battery
        // status may have changed.
        self.update_notification_responders();
    }
}

/// Creates a session control proxy from the Discovery protocol and connects to the session
/// specified by `id`.
fn create_session_control_proxy(
    discovery: DiscoveryProxy,
    id: MediaSessionId,
) -> Result<SessionControlProxy, Error> {
    let (session_proxy, session_request_stream) = create_proxy()?;
    discovery.connect_to_session(id.0, session_request_stream)?;
    Ok(session_proxy)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::media::media_state::tests::{create_metadata, create_player_status};
    use crate::media::media_types::ValidPlayStatus;
    use crate::tests::generate_empty_watch_notification;

    use assert_matches::assert_matches;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_avrcp::{NotificationEvent, TargetHandlerMarker};
    use fidl_fuchsia_media_sessions2 as fidl_media;
    use futures::{future::join_all, task::Poll};

    /// Creates the MediaSessions object and sets an active session if `is_active` = true.
    /// Returns the object and the id of the set active session.
    fn create_session(
        discovery: DiscoveryProxy,
        id: MediaSessionId,
        is_active: bool,
    ) -> MediaSessionsInner {
        let mut sessions = MediaSessionsInner::new();

        if is_active {
            sessions.active_session_id = Some(id.clone());
        }
        let create_res = sessions.create_or_update_session(
            discovery.clone(),
            id,
            SessionInfoDelta::EMPTY,
            &create_session_control_proxy,
        );
        assert_matches!(create_res, Ok(_));

        sessions
    }

    #[fuchsia::test]
    /// Normal case of registering a supported notification.
    /// Notification should should be packaged into a `NotificationData` and inserted
    /// into the HashMap.
    /// Since there are no state updates, it should stay there until the variable goes
    /// out of program scope.
    async fn test_register_notification_supported() {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Couldn't create discovery service endpoints.");
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");
        let disc_clone = discovery.clone();

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        {
            let supported_id = NotificationEvent::TrackChanged;
            // Create an active session.
            let id = MediaSessionId(1234);
            let mut inner = create_session(disc_clone.clone(), id, true);

            let current = fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::EMPTY
            };
            let res = inner.register_notification(supported_id, current.into(), 0, responder);
            assert_matches!(res, Ok(None));
            assert!(inner.notifications.contains_key(&supported_id));
            assert_eq!(
                1,
                inner
                    .notifications
                    .get(&supported_id)
                    .expect("Notification should be registered for this ID")
                    .len()
            );
        }

        // Drop is impl'd for NotificationData, so when `inner` is dropped when
        // `handle_n_watch_notifications` returns, the current value will be returned.
        let result = result_fut.await.expect("notification response");
        assert_eq!(
            result,
            Ok(fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::EMPTY
            })
        );
    }

    #[fuchsia::test]
    /// Test the insertion of a TrackPosChangedNotification.
    /// It should be successfully inserted, and a timeout duration should be returned.
    async fn test_register_notification_track_pos_changed() {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Couldn't create discovery service endpoints.");
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");
        let disc_clone = discovery.clone();

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        {
            // Create an active session.
            let id = MediaSessionId(1234);
            let mut inner = create_session(disc_clone.clone(), id, true);

            // Because this is TrackPosChanged, the given `current` data should be ignored.
            let ignored_current =
                fidl_avrcp::Notification { pos: Some(1234), ..fidl_avrcp::Notification::EMPTY };
            let supported_id = NotificationEvent::TrackPosChanged;
            // Register the TrackPosChanged with an interval of 2 seconds.
            let res =
                inner.register_notification(supported_id, ignored_current.into(), 2, responder);
            // Even though we provide a pos_change_interval of 2, playback is currently stopped,
            // so there is no deadline returned from `register_notification(..)`.
            assert_matches!(res, Ok(None));
            assert!(inner.notifications.contains_key(&supported_id));
            assert_eq!(
                1,
                inner
                    .notifications
                    .get(&supported_id)
                    .expect("Notification should be registered for this ID")
                    .len()
            );
        }

        // `watch_notification` should return the current value stored because no
        // state has changed.
        let result = result_fut.await.expect("notification response");
        assert_eq!(
            result,
            Ok(fidl_avrcp::Notification {
                pos: Some(std::u32::MAX),
                ..fidl_avrcp::Notification::EMPTY
            }),
        );
    }

    #[fuchsia::test]
    /// Test the insertion of a AddressedPlayerChanged notification.
    /// It should resolve immediately with a RejectedPlayerChanged because we only
    /// have at most one media player at all times.
    async fn test_register_notification_addressed_player_changed() {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Couldn't create discovery service endpoints.");
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");
        let disc_clone = discovery.clone();

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        // Create an active session.
        let id = MediaSessionId(1234);
        let mut inner = create_session(disc_clone.clone(), id, true);

        let supported_id = NotificationEvent::AddressedPlayerChanged;
        let res = inner.register_notification(
            supported_id,
            fidl_avrcp::Notification::EMPTY.into(),
            2,
            responder,
        );
        assert_matches!(res, Ok(None));
        assert!(!inner.notifications.contains_key(&supported_id));

        // Should return AddressedPlayerChanged.
        assert_matches!(
            result_fut.await.expect("Fidl call should work"),
            Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged)
        );
    }

    #[fuchsia::test]
    /// Test the insertion of a supported notification, but no active session.
    /// Upon insertion, the supported notification should be rejected and sent over
    /// the responder.
    async fn test_register_notification_no_active_session() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        {
            // Create state with no active session.
            let mut inner = MediaSessionsInner::new();

            let current = fidl_avrcp::Notification::EMPTY;
            let event_id = NotificationEvent::PlaybackStatusChanged;
            let res = inner.register_notification(event_id, current.into(), 0, responder);
            assert_matches!(res, Ok(None));
        }

        assert_matches!(
            result_fut.await.expect("Fidl call should work"),
            Err(fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers)
        );
    }

    #[fuchsia::test]
    /// Test the insertion of an unsupported notification.
    /// Upon insertion, the unsupported notification should be rejected, and the responder
    /// should immediately be called with a `RejectedInvalidParameter`.
    async fn test_register_notification_unsupported() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        {
            let mut inner = MediaSessionsInner::new();
            let unsupported_id = NotificationEvent::SystemStatusChanged;
            let current = fidl_avrcp::Notification::EMPTY;
            let res = inner.register_notification(unsupported_id, current.into(), 0, responder);
            assert_matches!(res, Ok(None));
        }

        assert_matches!(
            result_fut.await.expect("Fidl call should work"),
            Err(fidl_avrcp::TargetAvcError::RejectedInvalidParameter)
        );
    }

    #[fuchsia::test]
    /// 1. Test insertion of a new MediaSession update into the map. Creates a control
    /// proxy, and inserts into the state map. No outstanding notifications so no updates.
    /// 2. Test updating of an existing MediaSession in the map. The SessionInfo should change.
    async fn test_create_and_update_media_session() {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>().unwrap();

        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);
        assert!(sessions.map.contains_key(&id));

        let new_delta = fidl_media::SessionInfoDelta {
            player_status: Some(fidl_media::PlayerStatus {
                shuffle_on: Some(true),
                player_state: Some(fidl_media::PlayerState::Playing),
                ..fidl_media::PlayerStatus::EMPTY
            }),
            ..fidl_media::SessionInfoDelta::EMPTY
        };
        let update_res = sessions.create_or_update_session(
            discovery.clone(),
            id.clone(),
            new_delta,
            &create_session_control_proxy,
        );
        assert_matches!(update_res, Ok(_));
        assert!(sessions.map.contains_key(&id));

        let new_state = sessions
            .map
            .get(&id)
            .expect("There should be an existing session with id = `id`")
            .clone();
        assert_eq!(
            ValidPlayStatus::new(None, None, Some(fidl_avrcp::PlaybackStatus::Playing)),
            new_state.session_info().get_play_status().clone()
        );
    }

    #[fuchsia::test]
    /// Tests updating the active session_id correctly changes the currently
    /// playing active media session, as well as clears any outstanding notifications.
    /// 1. Test updating active_session_id with the same id does nothing.
    /// 2. Test updating active_session_id with a new id updates active id.
    async fn update_target_session_id() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);

        // 1. Update with the same id.
        let no_update = sessions.update_target_session_id(Some(id));
        assert_eq!(None, no_update);

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");
        {
            let supported_id = NotificationEvent::TrackChanged;
            let current = fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::EMPTY
            };
            let res = sessions.register_notification(supported_id, current.into(), 0, responder);
            assert_matches!(res, Ok(None));
            assert_eq!(
                1,
                sessions
                    .notifications
                    .get(&supported_id)
                    .expect("The notification should be registered")
                    .len()
            );

            // 2. Update with a new id.
            let new_id = MediaSessionId(9876);
            let expected_old_id = Some(id);
            let evicted_id = sessions.update_target_session_id(Some(new_id));
            assert_eq!(expected_old_id, evicted_id);
            assert_eq!(false, sessions.notifications.contains_key(&supported_id));
        }

        // The result of this should be a AddressedPlayerChanged, since we updated
        // the active session id amidst an outstanding notification.
        assert_matches!(
            result_fut.await.expect("Fidl call should work"),
            Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged)
        );
    }

    #[fuchsia::test]
    /// Tests that updating any outstanding responders behaves as expected.
    /// 1. Makes n calls to `watch_notification` to create n responders.
    /// 2. Inserts these responders into the map.
    /// 3. Mocks an update from MediaSession that changes internal state.
    /// 4. Updates all responders.
    /// 5. Ensures the resolved responders return the correct updated current notification
    /// values.
    async fn test_update_notification_responders() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create TargetHandler proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery, id, true);

        // Create n WatchNotification responders.
        let requested_event_ids = vec![
            NotificationEvent::PlayerApplicationSettingChanged,
            NotificationEvent::PlaybackStatusChanged,
            NotificationEvent::TrackChanged,
            NotificationEvent::TrackPosChanged,
            NotificationEvent::BattStatusChanged,
        ];
        let n: usize = requested_event_ids.len();
        let mut responders = vec![];
        let mut proxied_futs = vec![];

        for _ in 0..n {
            let (result_fut, responder) =
                generate_empty_watch_notification(&mut proxy, &mut stream)
                    .await
                    .expect("valid request");

            responders.push(responder);
            proxied_futs.push(result_fut);
        }

        {
            let ids = requested_event_ids.clone();
            for (event_id, responder) in ids.into_iter().zip(responders.into_iter()) {
                let current_val = match event_id {
                    NotificationEvent::TrackChanged => fidl_avrcp::Notification {
                        track_id: Some(std::u64::MAX),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    NotificationEvent::PlaybackStatusChanged => fidl_avrcp::Notification {
                        status: Some(fidl_avrcp::PlaybackStatus::Stopped),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    NotificationEvent::PlayerApplicationSettingChanged => {
                        fidl_avrcp::Notification {
                            application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                                shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                                repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                                ..fidl_avrcp::PlayerApplicationSettings::EMPTY
                            }),
                            ..fidl_avrcp::Notification::EMPTY
                        }
                    }
                    NotificationEvent::TrackPosChanged => fidl_avrcp::Notification {
                        pos: Some(std::u32::MAX),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    NotificationEvent::BattStatusChanged => fidl_avrcp::Notification {
                        battery_status: Some(fidl_avrcp::BatteryStatus::Normal),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    _ => fidl_avrcp::Notification::EMPTY,
                };
                // Register the notification event with responder.
                let res =
                    sessions.register_notification(event_id, current_val.into(), 10, responder);
                // None of the notifications in `requested_event_ids` requires a response deadline.
                assert_matches!(res, Ok(None));
                // The notification should be registered and there should only be one of each - no
                // duplicates provided in `requested_event_ids`.
                assert_eq!(
                    1,
                    sessions
                        .notifications
                        .get(&event_id)
                        .expect("The outstanding notification should exist in the map")
                        .len()
                );
            }
        }
        let delta = fidl_media::SessionInfoDelta {
            player_status: Some(create_player_status()),
            metadata: Some(create_metadata()),
            ..fidl_media::SessionInfoDelta::EMPTY
        };
        let new_battery_status = fidl_avrcp::BatteryStatus::FullCharge;

        // Update the local media state with session and battery changes.
        {
            let state = sessions.map.get_mut(&id).expect("active session for id exists");
            state.update_session_info(delta);
            state.update_battery_status(new_battery_status);
        }

        // Update all outstanding notifications.
        sessions.update_notification_responders();

        // All `n` notification requests should receive responses as the local session state has
        // changed with new values.
        let n_result_futs = join_all(proxied_futs).await;

        let expected_responses: Vec<(NotificationEvent, fidl_avrcp::Notification)> =
            requested_event_ids
                .into_iter()
                .zip(
                    vec![
                        fidl_avrcp::Notification {
                            application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                                shuffle_mode: Some(fidl_avrcp::ShuffleMode::AllTrackShuffle),
                                repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                                ..fidl_avrcp::PlayerApplicationSettings::EMPTY
                            }),
                            ..fidl_avrcp::Notification::EMPTY
                        },
                        fidl_avrcp::Notification {
                            status: Some(fidl_avrcp::PlaybackStatus::Playing),
                            ..fidl_avrcp::Notification::EMPTY
                        },
                        fidl_avrcp::Notification {
                            track_id: Some(0),
                            ..fidl_avrcp::Notification::EMPTY
                        },
                        fidl_avrcp::Notification {
                            pos: Some(0),
                            ..fidl_avrcp::Notification::EMPTY
                        }, // Ignored
                        fidl_avrcp::Notification {
                            battery_status: Some(new_battery_status),
                            ..fidl_avrcp::Notification::EMPTY
                        },
                    ]
                    .into_iter(),
                )
                .collect();

        let actual_responses: Vec<fidl_avrcp::Notification> = n_result_futs
            .into_iter()
            .map(|e1| e1.expect("FIDL call should work").expect("valid notification"))
            .collect();

        for (actual, (expected_id, expected)) in
            actual_responses.into_iter().zip(expected_responses.into_iter())
        {
            if expected_id == NotificationEvent::TrackPosChanged {
                // Since we aren't using fake time, just ensure the TrackPosChanged result exists.
                assert!(actual.pos.is_some());
            } else {
                assert_eq!(actual, expected);
            }
        }
    }

    #[fuchsia::test]
    async fn test_notification_update_with_unchanged_value_is_no_op() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>().unwrap();

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>().unwrap();
        let id = MediaSessionId(14);
        let mut sessions = create_session(discovery, id, true);

        let (result_fut, responder) = generate_empty_watch_notification(&mut proxy, &mut stream)
            .await
            .expect("valid request");

        let event_id = NotificationEvent::BattStatusChanged;
        let current_battery_status = fidl_avrcp::BatteryStatus::Critical;
        // Initialize the session state with an arbitrary battery level.
        {
            let state = sessions.map.get_mut(&id).expect("active session for id exists");
            state.update_battery_status(current_battery_status);
        }

        let current_val = fidl_avrcp::Notification {
            battery_status: Some(current_battery_status),
            ..fidl_avrcp::Notification::EMPTY
        };
        // Register the notification event with responder.
        let res = sessions.register_notification(event_id, current_val.into(), 10, responder);
        // Shouldn't require a response deadline.
        assert_matches!(res, Ok(None));
        // The notification should be registered.
        assert_eq!(1, sessions.notifications.get(&event_id).expect("Exists in map").len());

        // Updating the current state with the same battery status should not result in a
        // notification response.
        {
            let state = sessions.map.get_mut(&id).expect("active session for id exists");
            state.update_battery_status(current_battery_status);
        }
        sessions.update_notification_responders();

        assert_matches!(futures::poll!(result_fut), Poll::Pending);
        // The notification should still be registered.
        assert_eq!(1, sessions.notifications.get(&event_id).expect("Exists in map").len());
    }

    #[fuchsia::test]
    /// Tests `clear_notification_responders` correctly sends AddressedPlayerChanged
    /// error to all outstanding notifications.
    async fn test_clear_notification_responders() {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery, id, true);

        // Create `n` WatchNotification responders.
        let n: usize = 4;
        let mut responders = vec![];
        let mut proxied_futs = vec![];

        for _ in 0..n {
            let (result_fut, responder) =
                generate_empty_watch_notification(&mut proxy, &mut stream)
                    .await
                    .expect("valid request");

            responders.push(responder);
            proxied_futs.push(result_fut);
        }

        {
            let supported_event_ids = vec![
                NotificationEvent::TrackChanged,
                NotificationEvent::PlayerApplicationSettingChanged,
                NotificationEvent::PlayerApplicationSettingChanged,
                NotificationEvent::BattStatusChanged,
            ];
            // How many notifications we expect in the queue for entry at key = `event_id`.
            let expected_notification_queue_sizes = vec![1, 1, 2, 1];
            for (event_id, (responder, exp_size)) in supported_event_ids
                .into_iter()
                .zip(responders.into_iter().zip(expected_notification_queue_sizes.into_iter()))
            {
                let current_val = match event_id {
                    NotificationEvent::TrackChanged => fidl_avrcp::Notification {
                        track_id: Some(std::u64::MAX),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    NotificationEvent::PlayerApplicationSettingChanged => {
                        fidl_avrcp::Notification {
                            application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                                shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                                repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                                ..fidl_avrcp::PlayerApplicationSettings::EMPTY
                            }),
                            ..fidl_avrcp::Notification::EMPTY
                        }
                    }
                    NotificationEvent::BattStatusChanged => fidl_avrcp::Notification {
                        battery_status: Some(fidl_avrcp::BatteryStatus::Normal),
                        ..fidl_avrcp::Notification::EMPTY
                    },
                    _ => fidl_avrcp::Notification::EMPTY,
                };
                // Register the notification event with responder.
                let res =
                    sessions.register_notification(event_id, current_val.into(), 0, responder);
                assert_matches!(res, Ok(None));
                assert_eq!(
                    exp_size,
                    sessions
                        .notifications
                        .get(&event_id)
                        .expect("The outstanding notification should be in the map")
                        .len()
                );
            }

            // Clear all outstanding notifications.
            sessions.clear_notification_responders();
            assert!(sessions.notifications.is_empty());
        }

        // Should have a response on all 'n' watch calls.
        let n_result_futs = join_all(proxied_futs).await;

        // All of these should resolve to AddressedPlayerChanged since we are clearing
        // the outstanding notification queue.
        for r in n_result_futs {
            assert_matches!(
                r.expect("Fidl call should work"),
                Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged)
            );
        }
    }

    #[fuchsia::test]
    /// 1. Test clearing a session that doesn't exist in the map does nothing.
    /// 2. Test clearing an existing session that isn't active only removes session from map.
    /// 3. Test clearing an existing and active session updates `active_session_id` and
    /// removes from map.
    async fn test_clear_session() {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>().unwrap();

        // Create a new active session with default state.
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);
        assert!(sessions.map.contains_key(&id));

        let id2 = MediaSessionId(5678);
        let delta2 = SessionInfoDelta::EMPTY;
        let create_res2 = sessions.create_or_update_session(
            discovery.clone(),
            id2,
            delta2,
            &create_session_control_proxy,
        );
        assert_matches!(create_res2, Ok(_));
        assert!(sessions.map.contains_key(&id2));

        // 1. Dummy id.
        let unknown_id = MediaSessionId(999);
        let no_state = sessions.clear_session(&unknown_id);
        assert_eq!(Some(id), sessions.active_session_id);
        assert!(no_state.is_none());

        // 2. Existing, but not active id.
        let existing_state = sessions.clear_session(&id2);
        assert_eq!(Some(id), sessions.active_session_id);
        assert!(existing_state.is_some());

        // 3. Existing and active id.
        let active_state = sessions.clear_session(&id);
        assert_eq!(None, sessions.active_session_id);
        assert!(active_state.is_some());
    }

    #[fuchsia::test]
    /// We only support one player id: `MEDIA_SESSION_ADDRESSED_PLAYER_ID`, so any
    /// calls to `set_addressed_player` with a different ID should result in an error.
    async fn test_set_addressed_player() {
        let (discovery, _stream) = create_proxy::<DiscoveryMarker>().unwrap();

        // Create a new active session with default state.
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);

        let requested_player_id = fidl_avrcp::AddressedPlayerId { id: 10 };
        let res = sessions.set_addressed_player(requested_player_id);
        assert_matches!(res, Err(fidl_avrcp::TargetAvcError::RejectedInvalidPlayerId));

        let requested_player_id =
            fidl_avrcp::AddressedPlayerId { id: MEDIA_SESSION_ADDRESSED_PLAYER_ID };
        let res = sessions.set_addressed_player(requested_player_id);
        assert_eq!(res, Ok(()));

        // No active session.
        sessions = create_session(discovery.clone(), id, false);
        let requested_player_id =
            fidl_avrcp::AddressedPlayerId { id: MEDIA_SESSION_ADDRESSED_PLAYER_ID };
        let res = sessions.set_addressed_player(requested_player_id);
        assert_matches!(res, Err(fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers));
    }

    #[fuchsia::test]
    /// Getting the media items should return the same static response.
    async fn test_get_media_player_items() {
        let (discovery, _stream) = create_proxy::<DiscoveryMarker>().unwrap();

        // Create a new active session with default state.
        let id = MediaSessionId(1234);
        let sessions = create_session(discovery.clone(), id, true);

        let res = sessions.get_media_player_items();
        let expected = vec![fidl_avrcp::MediaPlayerItem {
            player_id: Some(MEDIA_SESSION_ADDRESSED_PLAYER_ID),
            major_type: Some(fidl_avrcp::MajorPlayerType::AUDIO),
            sub_type: Some(fidl_avrcp::PlayerSubType::empty()),
            playback_status: Some(fidl_avrcp::PlaybackStatus::Stopped),
            displayable_name: Some(MEDIA_SESSION_DISPLAYABLE_NAME.to_string()),
            ..fidl_avrcp::MediaPlayerItem::EMPTY
        }];
        assert_eq!(res, Ok(expected));
    }
}
