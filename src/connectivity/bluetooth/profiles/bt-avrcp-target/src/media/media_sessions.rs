// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl::encoding::Decodable as FidlDecodable,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp},
    fidl_fuchsia_media_sessions2::{
        DiscoveryMarker, DiscoveryProxy, SessionControlProxy, SessionInfoDelta,
        SessionsWatcherRequest, SessionsWatcherRequestStream, WatchOptions,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_warn, fx_vlog},
    futures::{Future, TryStreamExt},
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

use crate::media::media_state::MediaState;
use crate::media::media_types::Notification;
use crate::types::{
    bounded_queue::BoundedQueue, NotificationData, MAX_NOTIFICATION_EVENT_QUEUE_SIZE,
};

#[derive(Debug, Clone)]
pub(crate) struct MediaSessions {
    inner: Arc<RwLock<MediaSessionsInner>>,
}

impl MediaSessions {
    pub fn create() -> Self {
        Self { inner: Arc::new(RwLock::new(MediaSessionsInner::new())) }
    }

    // Returns a future that watches MediaPlayer for updates.
    pub fn watch(&self) -> impl Future<Output = Result<(), failure::Error>> {
        // MediaSession Service Setup
        // Set up the MediaSession Discovery service. Connect to the session watcher.
        let discovery = connect_to_service::<DiscoveryMarker>()
            .expect("Couldn't connect to discovery service.");
        let (watcher_client, watcher_requests) =
            create_request_stream().expect("Error creating watcher request stream");

        // Only subscribe to updates from players that are active.
        let watch_active_options =
            WatchOptions { only_active: Some(true), ..WatchOptions::new_empty() };
        discovery
            .watch_sessions(watch_active_options, watcher_client)
            .expect("Should watch media sessions");
        // End MediaSession Service Setup

        let inner = self.inner.clone();

        Self::watch_media_sessions(discovery, watcher_requests, inner)
    }

    pub fn get_active_session(&self) -> Result<MediaState, Error> {
        let r_inner = self.inner.read().get_active_session();
        r_inner.ok_or(format_err!("No active player"))
    }

    pub fn get_supported_notification_events(&self) -> Vec<fidl_avrcp::NotificationEvent> {
        self.inner.read().get_supported_notification_events()
    }

    pub fn register_notification(
        &self,
        event_id: fidl_avrcp::NotificationEvent,
        current: Notification,
        pos_change_interval: u32,
        responder: fidl_avrcp::TargetHandlerWatchNotificationResponder,
    ) -> Result<(), fidl::Error> {
        let mut write = self.inner.write();
        write.register_notification(event_id, current, pos_change_interval, responder)
    }

    async fn watch_media_sessions(
        discovery: DiscoveryProxy,
        mut watcher_requests: SessionsWatcherRequestStream,
        sessions_inner: Arc<RwLock<MediaSessionsInner>>,
    ) -> Result<(), failure::Error> {
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
                    fx_vlog!(tag: "avrcp-tg", 1, "MediaSession update: id[{}], delta[{:?}]", id, delta);

                    // Since we are only listening to active sessions, update the currently
                    // active media session id every time a watcher event is triggered.
                    // This means AVRCP commands will be queried/set to the player that has most
                    // recently changed in status.
                    sessions_inner.write().update_active_session_id(Some(id.clone()));

                    // If this is our first time receiving updates from this MediaPlayer, create
                    // a session control proxy and connect to the session.
                    sessions_inner.write().create_or_update_session(
                        discovery.clone(),
                        id.clone(),
                        delta,
                        &create_session_control_proxy,
                    )?;

                    fx_vlog!(tag: "avrcp-tg", 1, "MediaSession state after update: state[{:?}]", sessions_inner);
                }
                SessionsWatcherRequest::SessionRemoved { session_id, responder } => {
                    // A media session with id `session_id` has been removed.
                    responder.send()?;

                    // Clear any outstanding notifications with a player changed response.
                    // Clear the currently active session, if it equals `session_id`.
                    // Clear entry in state map.
                    sessions_inner.write().clear_session(&session_id);
                    fx_vlog!(tag: "avrcp-tg", 1, "Removed session [{:?}] from state map: {:?}", session_id, sessions_inner);
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
    active_session_id: Option<u64>,
    // The map of ids to the respective media session.
    map: HashMap<u64, MediaState>,
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

    /// TODO(41703): Add TRACK_POS_CHANGED when implemented.
    pub fn get_supported_notification_events(&self) -> Vec<fidl_avrcp::NotificationEvent> {
        vec![
            fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
            fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
            fidl_avrcp::NotificationEvent::TrackChanged,
        ]
    }

    /// Removes the MediaState specified by `id` from the map, should it exist.
    /// If the session was currently active, clears `self.active_session_id`.
    /// Returns the removed MediaState.
    pub fn clear_session(&mut self, id: &u64) -> Option<MediaState> {
        if Some(id) == self.active_session_id.as_ref() {
            self.update_active_session_id(None);
        }
        self.map.remove(id)
    }

    /// Clears all outstanding notifications with an AddressedPlayerChanged error.
    /// See `crate::types::update_responder` for more details.
    pub fn clear_notification_responders(&mut self) {
        for notif_data in self.notifications.drain().map(|(_, q)| q.into_iter()).flatten() {
            if let Err(e) = notif_data.update_responder(
                &fidl_avrcp::NotificationEvent::TrackChanged, // Irrelevant Event ID.
                Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged),
            ) {
                fx_log_warn!("There was an error clearing the responder: {:?}", e);
            }
        }
        fx_vlog!(tag: "avrcp-tg", 1, "After evicting cleared responders: {:?}", self.notifications);
    }

    /// Updates the active session with the new session specified by `id`.
    /// Clear all outstanding notifications, if the active session has changed.
    /// If the updated active session_id has changed, return old active id.
    pub fn update_active_session_id(&mut self, id: Option<u64>) -> Option<u64> {
        if self.active_session_id != id {
            self.clear_notification_responders();
            let previous_active_session_id = self.active_session_id.take();
            self.active_session_id = id;
            previous_active_session_id
        } else {
            None
        }
    }

    /// If an active session is present, update any outstanding notifications by
    /// checking if notification values have changed.
    /// TODO(41703): Take pos_change_interval into account when updating TRACK_POS_CHANGED.
    pub fn update_notification_responders(&mut self) {
        let state = if let Some(state) = self.get_active_session() {
            state.clone()
        } else {
            return;
        };

        self.notifications = self
            .notifications
            .drain()
            .map(|(event_id, queue)| {
                let curr_value = state.session_info().get_notification_value(&event_id);
                (
                    event_id,
                    queue
                        .into_iter()
                        .filter_map(|notif_data| {
                            notif_data
                                .update_responder(&event_id, curr_value.clone())
                                .unwrap_or(None)
                        })
                        .collect(),
                )
            })
            .collect();

        fx_vlog!(tag: "avrcp-tg", 1, "After evicting updated responders: {:?}", self.notifications);
    }

    /// If the entry, `id` doesn't exist in the map, create a `MediaState` entry
    /// when the control proxy.
    /// Update the state with the delta.
    /// Update any outstanding notification responders with the change in state.
    pub fn create_or_update_session<F>(
        &mut self,
        discovery: DiscoveryProxy,
        id: u64,
        delta: SessionInfoDelta,
        create_fn: F,
    ) -> Result<(), Error>
    where
        F: Fn(DiscoveryProxy, u64) -> Result<SessionControlProxy, Error>,
    {
        self.map
            .entry(id)
            .or_insert({
                let session_proxy = create_fn(discovery, id)?;
                MediaState::new(session_proxy)
            })
            .update_session_info(delta);

        self.update_notification_responders();
        Ok(())
    }

    /// Given a notification `event_id`:
    /// 1) insert it into the notifications map.
    /// 2) If the queue for `event_id` is full, evict the oldest responder and respond
    /// with the current value.
    /// 3) Update any outstanding notification responders with any changes in state.
    pub fn register_notification(
        &mut self,
        event_id: fidl_avrcp::NotificationEvent,
        current: Notification,
        pos_change_interval: u32,
        responder: fidl_avrcp::TargetHandlerWatchNotificationResponder,
    ) -> Result<(), fidl::Error> {
        // If the `event_id` is not supported, reject the registration.
        if !self.get_supported_notification_events().contains(&event_id) {
            return responder.send(&mut Err(fidl_avrcp::TargetAvcError::RejectedInvalidParameter));
        }

        let data = NotificationData::new(current, pos_change_interval, responder);

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

        Ok(())
    }
}

/// Creates a session control proxy from the Discovery protocol and connects to
/// the session specified by `id`.
fn create_session_control_proxy(
    discovery: DiscoveryProxy,
    id: u64,
) -> Result<SessionControlProxy, Error> {
    let (session_proxy, session_request_stream) = create_proxy()?;
    discovery.connect_to_session(id, session_request_stream)?;
    Ok(session_proxy)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::media::media_types::ValidPlayerApplicationSettings;

    use fidl::encoding::Decodable as FidlDecodable;
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_media::{self as fidl_media_types};
    use fidl_fuchsia_media_sessions2::{self as fidl_media, SessionControlMarker};
    use fuchsia_async as fasync;

    fn create_metadata() -> fidl_media_types::Metadata {
        let mut metadata = fidl_media_types::Metadata::new_empty();
        let mut property1 = fidl_media_types::Property::new_empty();
        property1.label = fidl_media_types::METADATA_LABEL_TITLE.to_string();
        let sample_title = "This is a sample title".to_string();
        property1.value = sample_title.clone();
        metadata.properties = vec![property1];
        metadata
    }

    fn create_player_status() -> fidl_media::PlayerStatus {
        let mut player_status = fidl_media::PlayerStatus::new_empty();

        let mut timeline_fn = fidl_media_types::TimelineFunction::new_empty();
        // Playback started at beginning of media.
        timeline_fn.subject_time = 0;
        // Monotonic clock time at beginning of media (nanos).
        timeline_fn.reference_time = 500000000;
        // Playback rate = 1, normal playback.
        timeline_fn.subject_delta = 1;
        timeline_fn.reference_delta = 1;

        player_status.player_state = Some(fidl_media::PlayerState::Playing);
        player_status.duration = Some(123456789);
        player_status.shuffle_on = Some(true);
        player_status.timeline_function = Some(timeline_fn);

        player_status
    }

    #[test]
    /// Test that retrieving a notification value correctly gets the current state.
    /// 1) Query with an unsupported `event_id`.
    /// 2) Query with a supported Event ID, with default state.
    /// 3) Query with all supported Event IDs.
    fn test_get_notification_value() {
        let exec = fasync::Executor::new_with_fake_time().expect("executor should build");
        exec.set_fake_time(fasync::Time::from_nanos(555555555));
        let media_sessions = MediaSessionsInner::new();
        let (session_proxy, _) =
            create_proxy::<SessionControlMarker>().expect("Couldn't create fidl proxy.");
        let mut media_state = MediaState::new(session_proxy);

        // 1. Unsupported ID.
        let unsupported_id = fidl_avrcp::NotificationEvent::BattStatusChanged;
        let res = media_state.session_info().get_notification_value(&unsupported_id);
        assert!(res.is_err());

        // 2. Supported ID, `media_state` contains default values.
        let res = media_state
            .session_info()
            .get_notification_value(&fidl_avrcp::NotificationEvent::PlaybackStatusChanged);
        assert_eq!(res.expect("Should be ok").status, Some(fidl_avrcp::PlaybackStatus::Stopped));
        let res = media_state
            .session_info()
            .get_notification_value(&fidl_avrcp::NotificationEvent::TrackChanged);
        assert_eq!(res.expect("Should be ok").track_id, Some(std::u64::MAX));

        // 3.
        exec.set_fake_time(fasync::Time::from_nanos(555555555));
        let mut info = fidl_media::SessionInfoDelta::new_empty();
        info.metadata = Some(create_metadata());
        info.player_status = Some(create_player_status());
        media_state.update_session_info(info);

        let expected_play_status = fidl_avrcp::PlaybackStatus::Playing;
        let expected_pas = ValidPlayerApplicationSettings::new(
            None,
            Some(fidl_avrcp::RepeatStatusMode::Off),
            Some(fidl_avrcp::ShuffleMode::AllTrackShuffle),
            None,
        );
        // Supported = PAS, Playback, Track, TrackPos
        let valid_events = media_sessions.get_supported_notification_events();
        let expected_values: Vec<Notification> = vec![
            Notification::new(None, None, None, Some(expected_pas), None, None, None),
            Notification::new(Some(expected_play_status), None, None, None, None, None, None),
            Notification::new(None, Some(0), None, None, None, None, None),
            Notification::new(None, None, Some(55), None, None, None, None),
        ];

        for (event_id, expected_v) in valid_events.iter().zip(expected_values.iter()) {
            assert_eq!(
                media_state.session_info().get_notification_value(&event_id).expect("Should work"),
                expected_v.clone()
            );
        }
    }

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests registering a notification works as expected.
    /// 1. Normal case, insertion of a supported notification.
    /// 2. Normal case, insertion of a supported notification, with eviction.
    /// 3. Normal case, insertion of a supported notification, with change in state,
    /// so that `update_notification_responders()` correctly updates inserted notif.
    /// 3. Error case, insertion of an unsupported notification.
    fn test_register_notification() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests insertion/updating of a new MediaSession into the state map.
    /// 1. Test branch where MediaSession already exists, so this is just an update.
    /// 2. Test branch where MediaSession doesn't exist, creates a new session and updates it.
    fn test_create_or_update_session() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests that updating any outstanding responders behaves as expected.
    fn test_update_notification_responders() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests updating the active session_id correctly changes the currently
    /// playing active media session, as well as clears any outstanding notifications
    /// if a new MediaSession becomes the active session.
    fn test_update_active_session_id() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests sending PlayerChanged response to all outstanding responders behaves
    /// as expected, and removes all entries in the Notifications map.
    fn test_clear_notification_responders() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests removing a session from the map.
    /// Tests clear_session clears all notifications if the MediaSession is the currently
    /// active session.
    fn test_clear_session() {}

    #[test]
    // TODO(42623): Implement this test as part of integration test work.
    /// Tests clearing the active session_id.
    fn test_clear_active_session_id() {}
}
