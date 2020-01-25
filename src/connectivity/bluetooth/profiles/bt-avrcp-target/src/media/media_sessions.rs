// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::encoding::Decodable as FidlDecodable,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp},
    fidl_fuchsia_media_sessions2::{
        DiscoveryMarker, DiscoveryProxy, SessionControlProxy, SessionInfoDelta,
        SessionsWatcherRequest, SessionsWatcherRequestStream, WatchOptions,
    },
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_warn, fx_vlog},
    fuchsia_zircon::DurationNum,
    futures::{future, Future, TryStreamExt},
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

use crate::media::media_state::MediaState;
use crate::media::media_types::Notification;
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

    // Returns a future that watches MediaPlayer for updates.
    pub fn watch(&self) -> impl Future<Output = Result<(), anyhow::Error>> + '_ {
        // MediaSession Service Setup
        // Set up the MediaSession Discovery service. Connect to the session watcher.
        let discovery = connect_to_service::<DiscoveryMarker>()
            .expect("Couldn't connect to discovery service.");
        let (watcher_client, watcher_requests) =
            create_request_stream().expect("Error creating watcher request stream");

        // Only subscribe to updates from players that are active.
        let watch_active_options =
            WatchOptions { only_active: Some(true), ..WatchOptions::new_empty() };
        if let Err(e) = discovery
            .watch_sessions(watch_active_options, watcher_client)
            .context("Should be able to watch media sessions")
        {
            fx_log_warn!("FIDL error watching sessions: {:?}", e);
        }
        // End MediaSession Service Setup

        self.watch_media_sessions(discovery, watcher_requests)
    }

    #[cfg(test)]
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

    /// Registers the notification and spawns a timeout task if needed.
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

        // If the `register_notification` call returned a timeout, spawn a task to
        // update any outstanding notifications at the deadline.
        if let Some(deadline) = timeout {
            let media_sessions = self.clone();
            let update_fut = future::pending().on_timeout(deadline, move || {
                media_sessions.inner.write().update_notification_responders();
            });
            fasync::spawn(update_fut);
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
                    fx_vlog!(tag: "avrcp-tg", 1, "MediaSession update: id[{}], delta[{:?}]", id, delta);

                    // Since we are only listening to active sessions, update the currently
                    // active media session id every time a watcher event is triggered.
                    // This means AVRCP commands will be queried/set to the player that has most
                    // recently changed in status.
                    sessions_inner.write().update_active_session_id(Some(MediaSessionId(id)));

                    // If this is our first time receiving updates from this MediaPlayer, create
                    // a session control proxy and connect to the session.
                    sessions_inner.write().create_or_update_session(
                        discovery.clone(),
                        MediaSessionId(id),
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
                    sessions_inner.write().clear_session(&MediaSessionId(session_id));
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
            fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
            fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
            fidl_avrcp::NotificationEvent::TrackChanged,
            fidl_avrcp::NotificationEvent::TrackPosChanged,
        ]
    }

    /// Removes the MediaState specified by `id` from the map, should it exist.
    /// If the session was currently active, clears `self.active_session_id`.
    /// Returns the removed MediaState.
    pub fn clear_session(&mut self, id: &MediaSessionId) -> Option<MediaState> {
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
    pub fn update_active_session_id(
        &mut self,
        id: Option<MediaSessionId>,
    ) -> Option<MediaSessionId> {
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

        fx_vlog!(tag: "avrcp-tg", 1, "After evicting updated responders: {:?}", self.notifications);
    }

    /// If the entry, `id` doesn't exist in the map, create a `MediaState` entry
    /// when the control proxy.
    /// Update the state with the delta.
    /// Update any outstanding notification responders with the change in state.
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

        self.update_notification_responders();
        Ok(())
    }

    /// Given a notification `event_id`:
    /// 1) Insert it into the notifications map.
    /// 2) If the queue for `event_id` is full, evict the oldest responder and respond
    /// with the current value.
    /// 3) Update any outstanding notification responders with any changes in state.
    /// 4) Return the (optional) notification response deadline.
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
        // For all other event_ids, use the provided `current` parameter, and
        // the `response_timeout` is not needed.
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
}

/// Creates a session control proxy from the Discovery protocol and connects to
/// the session specified by `id`.
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

    use fidl::encoding::Decodable as FidlDecodable;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_avrcp::TargetHandlerMarker;
    use fidl_fuchsia_media_sessions2 as fidl_media;
    use fuchsia_async as fasync;
    use futures::future::join_all;

    /// Creates the MediaSessions object and sets an active session if `is_active` = true.
    /// Returns the object and the id of the set active session.
    fn create_session(
        discovery: DiscoveryProxy,
        id: MediaSessionId,
        is_active: bool,
    ) -> MediaSessionsInner {
        let mut sessions = MediaSessionsInner::new();
        let delta = SessionInfoDelta::new_empty();

        if is_active {
            sessions.active_session_id = Some(id.clone());
        }
        let create_res = sessions.create_or_update_session(
            discovery.clone(),
            id,
            delta,
            &create_session_control_proxy,
        );
        assert_eq!(Ok(()), create_res.map_err(|e| e.to_string()));

        sessions
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Normal case of registering a supported notification.
    /// Notification should should be packaged into a `NotificationData` and inserted
    /// into the HashMap.
    /// Since there are no state updates, it should stay there until the variable goes
    /// out of program scope.
    async fn test_register_notification_supported() -> Result<(), Error> {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Couldn't create discovery service endpoints.");
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");
        let disc_clone = discovery.clone();

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            let supported_id = fidl_avrcp::NotificationEvent::TrackChanged;
            // Create an active session.
            let id = MediaSessionId(1234);
            let mut inner = create_session(disc_clone.clone(), id, true);

            let current = fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::new_empty()
            };
            let res = inner.register_notification(supported_id, current.into(), 0, responder);
            assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
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
        assert_eq!(
            Ok(Ok(fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::new_empty()
            })),
            result_fut.await.map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Test the insertion of a TrackPosChangedNotification.
    /// It should be successfully inserted, and a timeout duration should be returned.
    async fn test_register_notification_track_pos_changed() -> Result<(), Error> {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Couldn't create discovery service endpoints.");
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");
        let disc_clone = discovery.clone();

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            // Create an active session.
            let id = MediaSessionId(1234);
            let mut inner = create_session(disc_clone.clone(), id, true);

            // Because this is TrackPosChanged, the given `current` data should be ignored.
            let ignored_current = fidl_avrcp::Notification {
                pos: Some(1234),
                ..fidl_avrcp::Notification::new_empty()
            };
            let supported_id = fidl_avrcp::NotificationEvent::TrackPosChanged;
            // Register the TrackPosChanged with an interval of 2 seconds.
            let res =
                inner.register_notification(supported_id, ignored_current.into(), 2, responder);
            // Even though we provide a pos_change_interval of 2, playback is currently stopped,
            // so there is no deadline returned from `register_notification(..)`.
            assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
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
        assert_eq!(
            Ok(Ok(fidl_avrcp::Notification {
                pos: Some(std::u32::MAX),
                ..fidl_avrcp::Notification::new_empty()
            })),
            result_fut.await.map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Test the insertion of a supported notification, but no active session.
    /// Upon insertion, the supported notification should be rejected and sent over
    /// the responder.
    async fn test_register_notification_no_active_session() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            // Create state with no active session.
            let mut inner = MediaSessionsInner::new();

            let current = fidl_avrcp::Notification::new_empty();
            let event_id = fidl_avrcp::NotificationEvent::PlaybackStatusChanged;
            let res = inner.register_notification(event_id, current.into(), 0, responder);
            assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
        }

        assert_eq!(
            Err("RejectedNoAvailablePlayers".to_string()),
            result_fut.await.expect("Fidl call should work").map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Test the insertion of an unsupported notification.
    /// Upon insertion, the unsupported notification should be rejected, and the responder
    /// should immediately be called with a `RejectedInvalidParameter`.
    async fn test_register_notification_unsupported() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            let mut inner = MediaSessionsInner::new();
            let unsupported_id = fidl_avrcp::NotificationEvent::BattStatusChanged;
            let current = fidl_avrcp::Notification::new_empty();
            let res = inner.register_notification(unsupported_id, current.into(), 0, responder);
            assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
        }

        assert_eq!(
            Err("RejectedInvalidParameter".to_string()),
            result_fut.await.expect("FIDL call should work").map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// 1. Test insertion of a new MediaSession update into the map. Creates a control
    /// proxy, and inserts into the state map. No outstanding notifications so no updates.
    /// 2. Test updating of an existing MediaSession in the map. The SessionInfo should change.
    async fn test_create_and_update_media_session() -> Result<(), Error> {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()?;

        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);
        assert!(sessions.map.contains_key(&id));

        let new_delta = fidl_media::SessionInfoDelta {
            player_status: Some(fidl_media::PlayerStatus {
                shuffle_on: Some(true),
                player_state: Some(fidl_media::PlayerState::Playing),
                ..fidl_media::PlayerStatus::new_empty()
            }),
            ..fidl_media::SessionInfoDelta::new_empty()
        };
        let update_res = sessions.create_or_update_session(
            discovery.clone(),
            id.clone(),
            new_delta,
            &create_session_control_proxy,
        );
        assert_eq!(Ok(()), update_res.map_err(|e| e.to_string()));
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

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests updating the active session_id correctly changes the currently
    /// playing active media session, as well as clears any outstanding notifications.
    /// 1. Test updating active_session_id with the same id does nothing.
    /// 2. Test updating active_session_id with a new id updates active id.
    async fn test_update_active_session_id() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);

        // 1. Update with the same id.
        let no_update = sessions.update_active_session_id(Some(id));
        assert_eq!(None, no_update);

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;
        {
            let supported_id = fidl_avrcp::NotificationEvent::TrackChanged;
            let current = fidl_avrcp::Notification {
                track_id: Some(std::u64::MAX),
                ..fidl_avrcp::Notification::new_empty()
            };
            let res = sessions.register_notification(supported_id, current.into(), 0, responder);
            assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
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
            let evicted_id = sessions.update_active_session_id(Some(new_id));
            assert_eq!(expected_old_id, evicted_id);
            assert_eq!(false, sessions.notifications.contains_key(&supported_id));
        }

        // The result of this should be a AddressedPlayerChanged, since we updated
        // the active session id amidst an outstanding notification.
        assert_eq!(
            Err("RejectedAddressedPlayerChanged".to_string()),
            result_fut.await.expect("FIDL call should work").map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests that updating any outstanding responders behaves as expected.
    /// 1. Makes 3 calls to `watch_notification` to create 3 responders.
    /// 2. Inserts these responders into the map.
    /// 3. Mocks an update from MediaSession that changes internal state.
    /// 4. Updates all responders.
    /// 5. Ensures the resolved responders return the correct updated current notification
    /// values.
    async fn test_update_notification_responders() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create TargetHandler proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery, id, true);

        // Create 4 WatchNotification responders.
        let n: usize = 4;
        let mut responders = vec![];
        let mut proxied_futs = vec![];

        for _ in 0..n {
            let (result_fut, responder) =
                generate_empty_watch_notification(&mut proxy, &mut stream).await?;

            responders.push(responder);
            proxied_futs.push(result_fut);
        }

        {
            let supported_event_ids = sessions.get_supported_notification_events();
            for (event_id, responder) in supported_event_ids.into_iter().zip(responders.into_iter())
            {
                let current_val = match event_id {
                    fidl_avrcp::NotificationEvent::TrackChanged => fidl_avrcp::Notification {
                        track_id: Some(std::u64::MAX),
                        ..fidl_avrcp::Notification::new_empty()
                    },
                    fidl_avrcp::NotificationEvent::PlaybackStatusChanged => {
                        fidl_avrcp::Notification {
                            status: Some(fidl_avrcp::PlaybackStatus::Stopped),
                            ..fidl_avrcp::Notification::new_empty()
                        }
                    }
                    fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged => {
                        fidl_avrcp::Notification {
                            application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                                shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                                repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                                ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                            }),
                            ..fidl_avrcp::Notification::new_empty()
                        }
                    }
                    fidl_avrcp::NotificationEvent::TrackPosChanged => fidl_avrcp::Notification {
                        pos: Some(std::u32::MAX),
                        ..fidl_avrcp::Notification::new_empty()
                    },
                    _ => fidl_avrcp::Notification::new_empty(),
                };
                // Register the notification event with responder.
                let res =
                    sessions.register_notification(event_id, current_val.into(), 10, responder);
                assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
                assert_eq!(
                    1,
                    sessions
                        .notifications
                        .get(&event_id)
                        .expect("The outstanding notification should exist in the map")
                        .len()
                );
            }

            let delta = fidl_media::SessionInfoDelta {
                player_status: Some(create_player_status()),
                metadata: Some(create_metadata()),
                ..fidl_media::SessionInfoDelta::new_empty()
            };

            // First, update state values.
            if let Some(state) = sessions.map.get_mut(&id) {
                state.update_session_info(delta);
            }

            // Then, update all outstanding notifications.
            sessions.update_notification_responders();
        }

        // Should have a response on all 'n' watch calls.
        let n_result_futs = join_all(proxied_futs).await;

        let expected: Result<Vec<fidl_avrcp::Notification>, String> = Ok(vec![
            fidl_avrcp::Notification {
                application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                    shuffle_mode: Some(fidl_avrcp::ShuffleMode::AllTrackShuffle),
                    repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                    ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                }),
                ..fidl_avrcp::Notification::new_empty()
            },
            fidl_avrcp::Notification {
                status: Some(fidl_avrcp::PlaybackStatus::Playing),
                ..fidl_avrcp::Notification::new_empty()
            },
            fidl_avrcp::Notification { track_id: Some(0), ..fidl_avrcp::Notification::new_empty() },
        ]);

        let response: Result<Vec<fidl_avrcp::Notification>, String> = n_result_futs
            .into_iter()
            .map(|e1| e1.expect("FIDL call should work").map_err(|e| format!("{:?}", e)))
            .collect();
        let mut response_inner = response.expect("n FIDL calls should work");

        // Since we aren't using fake time, just ensure the TrackPosChanged result exists.
        let track_pos_response = response_inner.pop().expect("Notification should exist");
        assert!(track_pos_response.pos.is_some());
        assert_eq!(expected, Ok(response_inner));

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests `clear_notification_responders` correctly sends AddressedPlayerChanged
    /// error to all outstanding notifications.
    async fn test_clear_notification_responders() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        // Create a new active session with default state.
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()
            .expect("Discovery service should be able to be created");
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery, id, true);

        // Create 3 WatchNotification responders.
        let n: usize = 3;
        let mut responders = vec![];
        let mut proxied_futs = vec![];

        for _ in 0..n {
            let (result_fut, responder) =
                generate_empty_watch_notification(&mut proxy, &mut stream).await?;

            responders.push(responder);
            proxied_futs.push(result_fut);
        }

        {
            let supported_event_ids = vec![
                fidl_avrcp::NotificationEvent::TrackChanged,
                fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
                fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
            ];
            // How many notifications we expect in the queue for entry at key = `event_id`.
            let expected_notification_queue_sizes = vec![1, 1, 2];
            for (event_id, (responder, exp_size)) in supported_event_ids
                .into_iter()
                .zip(responders.into_iter().zip(expected_notification_queue_sizes.into_iter()))
            {
                let current_val = match event_id {
                    fidl_avrcp::NotificationEvent::TrackChanged => fidl_avrcp::Notification {
                        track_id: Some(std::u64::MAX),
                        ..fidl_avrcp::Notification::new_empty()
                    },
                    fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged => {
                        fidl_avrcp::Notification {
                            application_settings: Some(fidl_avrcp::PlayerApplicationSettings {
                                shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                                repeat_status_mode: Some(fidl_avrcp::RepeatStatusMode::Off),
                                ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                            }),
                            ..fidl_avrcp::Notification::new_empty()
                        }
                    }
                    _ => fidl_avrcp::Notification::new_empty(),
                };
                // Register the notification event with responder.
                let res =
                    sessions.register_notification(event_id, current_val.into(), 0, responder);
                assert_eq!(Ok(None), res.map_err(|e| e.to_string()));
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
            assert_eq!(
                Err("RejectedAddressedPlayerChanged".to_string()),
                r.expect("FIDL call should work").map_err(|e| format!("{:?}", e))
            );
        }

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// 1. Test clearing a session that doesn't exist in the map does nothing.
    /// 2. Test clearing an existing session that isn't active only removes session from map.
    /// 3. Test clearing an existing and active session updates `active_session_id` and
    /// removes from map.
    async fn test_clear_session() -> Result<(), Error> {
        let (discovery, _request_stream) = create_proxy::<DiscoveryMarker>()?;

        // Create a new active session with default state.
        let id = MediaSessionId(1234);
        let mut sessions = create_session(discovery.clone(), id, true);
        assert!(sessions.map.contains_key(&id));

        let id2 = MediaSessionId(5678);
        let delta2 = SessionInfoDelta::new_empty();
        let create_res2 = sessions.create_or_update_session(
            discovery.clone(),
            id2,
            delta2,
            &create_session_control_proxy,
        );
        assert_eq!(Ok(()), create_res2.map_err(|e| e.to_string()));
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

        Ok(())
    }
}
