// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{clone_session_id_handle, MAX_EVENTS_SENT_WITHOUT_ACK};
use failure::{Error, ResultExt};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_media::TimelineFunction;
use fidl_fuchsia_mediasession::{
    PlaybackCapabilities, PlaybackCapabilityFlags, PlaybackState, PlaybackStatus, PublisherMarker,
    PublisherProxy, RegistryEvent, RegistryEventStream, RegistryMarker, RegistryProxy, RepeatMode,
    SessionControlHandle, SessionDelta, SessionEntry, SessionEvent, SessionMarker, SessionRequest,
    SessionRequestStream, SessionsChange,
};
use fuchsia_app as app;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    select,
    stream::{FusedStream, TryStreamExt},
    FutureExt, StreamExt,
};
use zx::AsHandleRef;

const MEDIASESSION_URL: &str = "fuchsia-pkg://fuchsia.com/mediasession#meta/mediasession.cmx";

fn clone_session_entry(entry: &SessionEntry) -> Result<SessionEntry, Error> {
    Ok(SessionEntry {
        session_id: entry.session_id.as_ref().map(clone_session_id_handle).transpose()?,
        local: entry.local.clone(),
    })
}

fn default_playback_status() -> PlaybackStatus {
    PlaybackStatus {
        duration: Some(100),
        playback_state: Some(PlaybackState::Playing),
        playback_function: Some(TimelineFunction {
            subject_time: 0,
            reference_time: 0,
            subject_delta: 1,
            reference_delta: 1,
        }),
        repeat_mode: Some(RepeatMode::Off),
        shuffle_on: Some(false),
        has_next_item: Some(true),
        has_prev_item: Some(false),
        error: None,
    }
}

/// Checks two session entries and returns true if they are equal.
fn are_session_entries_equal(actual: &SessionEntry, expected: &SessionEntry) -> bool {
    assert!(
        actual.session_id.is_some(),
        "Media Session should never omit the session id from the entry."
    );
    let actual_koid = actual
        .session_id
        .as_ref()
        .map(|id| id.as_handle_ref().get_koid().expect("Handle actual KOID"));
    let expected_koid = expected
        .session_id
        .as_ref()
        .map(|id| id.as_handle_ref().get_koid().expect("Handle expected KOID"));
    actual.local == expected.local && actual_koid == expected_koid
}

fn are_session_entry_lists_equal(actual: Vec<SessionEntry>, expected_sessions: &[SessionEntry]) {
    assert_eq!(
        actual.len(),
        expected_sessions.len(),
        "Actual: {:?}\nExpected: {:?}",
        actual,
        expected_sessions
    );
    for expected in expected_sessions.iter() {
        assert!(
            actual.iter().find(|actual| are_session_entries_equal(actual, expected)).is_some(),
            "Actual: {:?}\nExpected: {:?}",
            actual,
            expected_sessions
        );
    }
}

struct TestSession {
    request_stream: SessionRequestStream,
    control_handle: SessionControlHandle,
    client_end: ClientEnd<SessionMarker>,
}

impl TestSession {
    fn new() -> Result<Self, Error> {
        let (client_end, server_end) = create_endpoints::<SessionMarker>().expect("Fidl endpoints");

        let (request_stream, control_handle) =
            server_end.into_stream_and_control_handle().context("Unpacking Session server end")?;

        Ok(Self { request_stream, control_handle, client_end })
    }
}

struct TestService {
    // This needs to stay alive to keep the service running.
    #[allow(unused)]
    app: app::client::App,
    publisher: PublisherProxy,
    registry: RegistryProxy,
    registry_events: RegistryEventStream,
}

impl TestService {
    fn new() -> Result<Self, Error> {
        let launcher = app::client::Launcher::new().context("Creating launcher")?;
        let mediasession = launcher
            .launch(String::from(MEDIASESSION_URL), None)
            .context("Launching mediasession")?;

        let publisher =
            mediasession.connect_to_service(PublisherMarker).context("Connecting to Publisher")?;
        let registry =
            mediasession.connect_to_service(RegistryMarker).context("Connecting to Registry")?;
        let registry_events = registry.take_event_stream();

        Ok(Self { app: mediasession, publisher, registry, registry_events })
    }

    /// Consumes the next active session event and compares the Koids with the
    /// expectation.
    async fn expect_active_session(&mut self, expected: Option<zx::Koid>) {
        assert!(!self.registry_events.is_terminated());
        let event = await!(self.registry_events.try_next())
            .expect("Registry event")
            .expect("Registry event stream");
        match event {
            RegistryEvent::OnActiveSessionChanged { active_session: actual } => {
                let actual = actual.session_id.map(|session_id| {
                    session_id.as_handle_ref().get_koid().expect("Handle actual KOID")
                });
                assert_eq!(actual, expected);
            }
            other => panic!("Expected active session event; got {:?}", other),
        }
    }

    async fn expect_sessions_change(&mut self, expected_change: SessionsChange) {
        assert!(!self.registry_events.is_terminated());
        let event = await!(self.registry_events.try_next())
            .expect("Registry event")
            .expect("Registry event stream");
        match event {
            RegistryEvent::OnSessionsChanged { sessions_change } => {
                match (&sessions_change, &expected_change) {
                    (
                        SessionsChange { session: ref actual, delta: SessionDelta::Added },
                        SessionsChange { session: ref expected, delta: SessionDelta::Added },
                    ) => {
                        assert!(
                            are_session_entries_equal(actual, expected),
                            "Actual: {:?}\nExpected: {:?}",
                            sessions_change,
                            expected_change
                        );
                    }
                    (
                        SessionsChange { session: ref actual, delta: SessionDelta::Removed },
                        SessionsChange { session: ref expected, delta: SessionDelta::Removed },
                    ) => {
                        assert!(
                            are_session_entries_equal(actual, expected),
                            "Actual: {:?}\nExpected: {:?}",
                            sessions_change,
                            expected_change
                        );
                    }
                    _ => panic!("Expected {:?}; got {:?}", expected_change, sessions_change),
                }
            }
            other => panic!("Expected session list; got {:?}", other),
        }
    }

    /// Consumes the next two events in any order and expects them to be the
    /// expected active session and be expected session change.
    async fn expect_update_events(
        &mut self,
        expected_active_session: Option<zx::Koid>,
        expected_change: SessionsChange,
    ) {
        assert!(!self.registry_events.is_terminated());
        for _ in 0..2 {
            let event = await!(self.registry_events.try_next())
                .expect("Registry event")
                .expect("Registry event stream");
            match event {
                RegistryEvent::OnActiveSessionChanged { active_session: actual } => {
                    let actual = actual.session_id.map(|session_id| {
                        session_id.as_handle_ref().get_koid().expect("Handle actual KOID")
                    });
                    assert_eq!(actual, expected_active_session);
                }
                RegistryEvent::OnSessionsChanged { sessions_change } => {
                    match (&sessions_change, &expected_change) {
                        (
                            SessionsChange { session: ref actual, delta: SessionDelta::Added },
                            SessionsChange { session: ref expected, delta: SessionDelta::Added },
                        ) => {
                            assert!(
                                are_session_entries_equal(actual, expected),
                                "Actual: {:?}\nExpected: {:?}",
                                sessions_change,
                                expected_change
                            );
                        }
                        (
                            SessionsChange { session: ref actual, delta: SessionDelta::Removed },
                            SessionsChange { session: ref expected, delta: SessionDelta::Removed },
                        ) => {
                            assert!(
                                are_session_entries_equal(actual, expected),
                                "Actual: {:?}\nExpected: {:?}",
                                sessions_change,
                                expected_change
                            );
                        }
                        _ => panic!("Expected {:?}; got {:?}", expected_change, sessions_change),
                    }
                }
            }
        }
    }

    // Gets the full list of sessions from the service and expects it to be
    // equal to the expectation, without respect to order.
    async fn expect_session_list(&mut self, expected_sessions: Vec<SessionEntry>) {
        let new_client = self
            .app
            .connect_to_service(RegistryMarker)
            .context("Connecting to Registry")
            .expect("New registry client");
        let mut new_client_events = new_client.take_event_stream();
        let mut sessions = vec![];

        while sessions.len() < expected_sessions.len() {
            let event = await!(new_client_events.try_next())
                .expect("Registry event")
                .expect("Registry event stream");
            match event {
                RegistryEvent::OnSessionsChanged { sessions_change } => {
                    if let SessionsChange { session, delta: SessionDelta::Added } = sessions_change
                    {
                        sessions.push(session);
                    }
                }
                _ => {}
            };
            new_client.notify_sessions_change_handled().expect("To ack events");
        }

        are_session_entry_lists_equal(sessions, &expected_sessions);
    }

    fn notify_events_handled(&mut self) {
        self.notify_active_session_change_handled();
        self.notify_sessions_change_handled();
    }

    fn notify_active_session_change_handled(&mut self) {
        self.registry
            .notify_active_session_change_handled()
            .expect("To notify service active session change was handled");
    }

    fn notify_sessions_change_handled(&mut self) {
        self.registry
            .notify_sessions_change_handled()
            .expect("To notify service sessions change handled");
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn empty_service_reports_no_sessions() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    await!(test_service.expect_session_list(vec![]));
}

#[fasync::run_singlethreaded]
#[test]
async fn service_routes_controls() {
    let test_service = TestService::new().expect("Test service");

    // Creates a new session and publishes it. Returns the proxy through Media
    // Session service and the request stream on the backend.
    let new_session = || {
        async {
            let test_session = TestSession::new().expect("Test session");
            let session_id = await!(test_service.publisher.publish(test_session.client_end))
                .expect("Session id");
            let (client_end, server_end) =
                create_endpoints::<SessionMarker>().expect("Session endpoints");
            test_service
                .registry
                .connect_to_session_by_id(session_id, server_end)
                .expect("To connect to session");
            let proxy = client_end.into_proxy().expect("Session a proxy");
            (proxy, test_session.request_stream)
        }
    };

    let (proxy_a, mut request_stream_a) = await!(new_session());
    let (proxy_b, mut request_stream_b) = await!(new_session());

    proxy_a.play().expect("To call Play() on Session a");
    proxy_b.pause().expect("To call Pause() on Session b");

    let a_event = await!(request_stream_a.try_next()).expect("Next request from session a");
    let b_event = await!(request_stream_b.try_next()).expect("Next request from session b");

    assert!(match a_event {
        Some(SessionRequest::Play { .. }) => true,
        _ => false,
    },);

    assert!(match b_event {
        Some(SessionRequest::Pause { .. }) => true,
        _ => false,
    },);

    // Ensure the behaviour continues.

    proxy_b.play().expect("To call Play() on Session b");

    let b_event = await!(request_stream_b.try_next()).expect("Next request from session b");

    assert!(match b_event {
        Some(SessionRequest::Play { .. }) => true,
        _ => false,
    },);
}

#[fasync::run_singlethreaded]
#[test]
async fn service_reports_published_active_session() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    test_service.notify_events_handled();

    let test_session = TestSession::new().expect("Test session");
    let our_session_id =
        await!(test_service.publisher.publish(test_session.client_end)).expect("Session id");
    test_session
        .control_handle
        .send_on_playback_status_changed(default_playback_status())
        .expect("To update playback status");
    await!(test_service.expect_update_events(
        Some(our_session_id.as_handle_ref().get_koid().expect("Handle expected KOID")),
        SessionsChange {
            session: SessionEntry { session_id: Some(our_session_id), local: Some(true) },
            delta: SessionDelta::Added,
        }
    ));
}

#[fasync::run_singlethreaded]
#[test]
async fn nonlocal_session_does_not_compete_for_active() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    test_service.notify_events_handled();

    // Register a remote session.
    let remote_test_session = TestSession::new().expect("Test session");
    let remote_session_id =
        await!(test_service.publisher.publish_remote(remote_test_session.client_end))
            .expect("Remote session id");
    remote_test_session
        .control_handle
        .send_on_playback_status_changed(default_playback_status())
        .expect("To update remote playback status");
    await!(test_service.expect_sessions_change(SessionsChange {
        session: SessionEntry { session_id: Some(remote_session_id), local: Some(false) },
        delta: SessionDelta::Added
    }));
    test_service.notify_events_handled();

    // Register a local session.
    let local_test_session = TestSession::new().expect("Test session");
    let local_session_id = await!(test_service.publisher.publish(local_test_session.client_end))
        .expect("Local session id");
    local_test_session
        .control_handle
        .send_on_playback_status_changed(default_playback_status())
        .expect("To update local playback status");

    // Ensure that the only active session update we get is for the local session.
    await!(test_service.expect_update_events(
        Some(local_session_id.as_handle_ref().get_koid().expect("Handle expected KOID")),
        SessionsChange {
            session: SessionEntry { session_id: Some(local_session_id), local: Some(true) },
            delta: SessionDelta::Added
        }
    ));
}

#[fasync::run_singlethreaded]
#[test]
async fn service_reports_changed_active_session() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    test_service.notify_events_handled();

    // Publish sessions.
    let session_count: usize = 100;
    let mut keep_alive = Vec::new();
    for i in 0..session_count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("Session {}", i));
        test_session
            .control_handle
            .send_on_playback_status_changed(default_playback_status())
            .expect("To update playback status");
        await!(test_service.expect_update_events(
            Some(session_id.as_handle_ref().get_koid().expect("Handle expected KOID")),
            SessionsChange {
                session: SessionEntry { session_id: Some(session_id), local: Some(true) },
                delta: SessionDelta::Added,
            }
        ));
        test_service.notify_events_handled();
        keep_alive.push(test_session.control_handle);
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_broadcasts_events() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    test_service.notify_events_handled();

    let test_session = TestSession::new().expect("Test session");
    let session_id = await!(test_service.publisher.publish(test_session.client_end))
        .expect(&format!("To publish session"));

    await!(test_service.expect_sessions_change(SessionsChange {
        session: SessionEntry {
            session_id: Some(
                clone_session_id_handle(&session_id).expect("Duplicate session handle")
            ),
            local: Some(true)
        },
        delta: SessionDelta::Added
    }));
    test_service.notify_events_handled();

    let expected_playback_capabilities = || PlaybackCapabilities {
        flags: Some(PlaybackCapabilityFlags::Play | PlaybackCapabilityFlags::Pause),
        supported_skip_intervals: Some(vec![23, 34]),
        supported_playback_rates: Some(vec![10.0, 20.0]),
        supported_repeat_modes: Some(vec![RepeatMode::Off]),
        custom_extensions: Some(vec![String::from("1"), String::from("2")]),
    };

    test_session
        .control_handle
        .send_on_playback_status_changed(default_playback_status())
        .expect("To update playback status");

    test_session
        .control_handle
        .send_on_playback_capabilities_changed(expected_playback_capabilities())
        .expect("To update playback capabilities");

    // Ensure we wait for the service to accept the session.
    await!(test_service.expect_active_session(Some(
        session_id.as_handle_ref().get_koid().expect("Handle expected KOID")
    )));
    test_service.notify_events_handled();

    // Connect many clients and ensure they all receive the event.
    let client_count: usize = 100;
    for _ in 0..client_count {
        let (client_end, server_end) =
            create_endpoints::<SessionMarker>().expect("Session endpoints");
        test_service
            .registry
            .connect_to_session_by_id(
                clone_session_id_handle(&session_id).expect("Duplicate session handle"),
                server_end,
            )
            .expect("To connect to session");
        let mut event_stream = client_end.into_proxy().expect("Session proxy").take_event_stream();
        let check_event = |event: Option<SessionEvent>| {
            assert!(event
                .and_then(|event| match event {
                    SessionEvent::OnPlaybackStatusChanged { playback_status } => {
                        Some(playback_status == default_playback_status())
                    }
                    SessionEvent::OnPlaybackCapabilitiesChanged { playback_capabilities } => {
                        Some(playback_capabilities == expected_playback_capabilities())
                    }
                    _ => None,
                })
                .unwrap_or(false));
        };

        // Expect we get both of our published events; accept any order.
        check_event(await!(event_stream.try_next()).expect("Next Session event"));
        check_event(await!(event_stream.try_next()).expect("Next Session event"));
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_correctly_tracks_session_ids_states_and_lifetimes() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    await!(test_service.expect_session_list(vec![]));

    // Publish many sessions and have each of them post a playback status.
    let count = 100;
    let mut test_sessions = Vec::new();
    let numbered_playback_status =
        |i| PlaybackStatus { duration: Some(i), ..default_playback_status() };
    for i in 0..count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("To publish test session {}.", i));
        test_session
            .control_handle
            .send_on_playback_status_changed(numbered_playback_status(i as i64))
            .expect(&format!("To broadcast playback status {}.", i));
        test_sessions.push((
            clone_session_id_handle(&session_id).expect("Cloned id handle"),
            test_session.control_handle,
        ));

        await!(test_service.expect_update_events(
            Some(session_id.as_handle_ref().get_koid().expect("New active session koid")),
            SessionsChange {
                session: SessionEntry {
                    session_id: Some(
                        clone_session_id_handle(&session_id).expect("Cloned id handle"),
                    ),
                    local: Some(true),
                },
                delta: SessionDelta::Added,
            },
        ));
        test_service.notify_events_handled();
    }

    enum Expectation {
        SessionIsDropped,
        SessionReportsPlaybackStatus(PlaybackStatus),
    }

    // Set up expectations.
    let mut expectations = Vec::new();
    let mut control_handles_to_keep_sessions_alive = Vec::new();
    for (i, (session_id, control_handle)) in test_sessions.into_iter().enumerate() {
        let should_drop = i % 3 == 0;
        expectations.push(if should_drop {
            (Expectation::SessionIsDropped, session_id)
        } else {
            control_handles_to_keep_sessions_alive.push(control_handle);
            (
                Expectation::SessionReportsPlaybackStatus(numbered_playback_status(i as i64)),
                session_id,
            )
        });
        test_service.notify_events_handled();
    }

    // Check all expectations.
    for (expectation, session_id) in expectations.into_iter() {
        let (client_end, server_end) = create_endpoints::<SessionMarker>().expect("Fidl endpoints");
        test_service
            .registry
            .connect_to_session_by_id(
                clone_session_id_handle(&session_id).expect("Duplicate session handle"),
                server_end,
            )
            .expect(&format!("To make connection request to session {:?}", session_id));
        let mut event_stream = client_end
            .into_proxy()
            .expect(&format!("Session proxy for session {:?}.", session_id))
            .take_event_stream();
        let maybe_event = await!(event_stream.try_next()).expect("Next session event");
        match expectation {
            Expectation::SessionIsDropped => {
                // If we shutdown the session, this or the next event should be
                // None depending on whether our shutdown reached the service
                // before this request.
                if maybe_event.is_some() {
                    let next_event = await!(event_stream.try_next()).expect("Next session event");
                    assert!(next_event.is_none(), "{:?}", next_event)
                }
            }
            Expectation::SessionReportsPlaybackStatus(expected) => match maybe_event {
                Some(SessionEvent::OnPlaybackStatusChanged { playback_status: actual }) => {
                    assert_eq!(actual, expected)
                }
                other => panic!("Expected a playback status event; got: {:?}", other),
            },
        }
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_stops_sending_sessions_change_events_to_inactive_clients() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    await!(test_service.expect_session_list(vec![]));
    test_service.notify_events_handled();

    // Force the service to generate MAX_EVENTS_SENT_WITHOUT_ACK + 1 events to
    // send us, and consume all but the last of them.
    let count = MAX_EVENTS_SENT_WITHOUT_ACK + 1;
    let mut test_sessions = Vec::new();
    let mut expected_sessions = Vec::new();
    for i in 0..count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("To publish test session {}.", i));
        let entry = SessionEntry { session_id: Some(session_id), local: Some(true) };
        test_sessions.push(test_session.control_handle);
        expected_sessions.push(
            clone_session_entry(&entry).expect(&format!("Clone of test session entry {}.", i)),
        );

        test_service.notify_active_session_change_handled();
        if i < MAX_EVENTS_SENT_WITHOUT_ACK {
            await!(test_service.expect_sessions_change(SessionsChange {
                session: entry,
                delta: SessionDelta::Added
            }));
        }
    }

    select! {
        e = test_service.registry_events.select_next_some() => {
            panic!("Received event even though we didn't ack: {:?}, sessions registered: {:?}",
                   e, expected_sessions);
        }
        _ = fasync::Timer::new(zx::Time::after(zx::Duration::from_millis(2))).fuse() => {
            return;
        }
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_stops_sending_active_session_change_events_to_inactive_clients() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    await!(test_service.expect_session_list(vec![]));
    test_service.notify_events_handled();

    // Force the service to generate MAX_EVENTS_SENT_WITHOUT_ACK + 1 events to
    // send us, and consume all but the last of them.
    let count = MAX_EVENTS_SENT_WITHOUT_ACK + 1;
    let mut test_sessions = Vec::new();
    let mut expected_sessions = Vec::new();
    for i in 0..count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("To publish test session {}.", i));
        test_session
            .control_handle
            .send_on_playback_status_changed(default_playback_status())
            .expect(&format!("To broadcast playback status {}.", i));
        let entry = SessionEntry {
            session_id: Some(clone_session_id_handle(&session_id).expect("handle clone")),
            local: Some(true),
        };
        test_sessions.push(test_session.control_handle);
        expected_sessions.push(
            clone_session_entry(&entry).expect(&format!("Clone of test session entry {}.", i)),
        );

        if i < MAX_EVENTS_SENT_WITHOUT_ACK {
            await!(test_service.expect_update_events(
                Some(session_id.as_handle_ref().get_koid().expect("New active session koid")),
                SessionsChange { session: entry, delta: SessionDelta::Added }
            ));
        } else {
            await!(test_service.expect_sessions_change(SessionsChange {
                session: entry,
                delta: SessionDelta::Added
            }));
        }
        test_service.notify_sessions_change_handled();
    }

    select! {
        _ = test_service.registry_events.select_next_some() => {
            panic!("Received event from service even though we didn't ack");
        }
        _ = fasync::Timer::new(zx::Time::after(zx::Duration::from_millis(2))).fuse() => {
            return;
        }
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_maintains_session_list() {
    let mut test_service = TestService::new().expect("Test service");
    await!(test_service.expect_active_session(None));
    await!(test_service.expect_session_list(vec![]));
    test_service.notify_events_handled();

    // Publish many sessions and check at each point that the session list is
    // accurate.
    let count = 30;
    let mut test_sessions = Vec::new();
    let mut expected_sessions = Vec::new();
    for i in 0..count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("To publish test session {}.", i));
        let entry = SessionEntry { session_id: Some(session_id), local: Some(true) };
        expected_sessions.push(
            clone_session_entry(&entry).expect(&format!("Clone of test session entry {}.", i)),
        );
        test_sessions.push(test_session.control_handle);
        await!(test_service
            .expect_sessions_change(SessionsChange { session: entry, delta: SessionDelta::Added }));
        test_service.notify_events_handled();
        await!(test_service.expect_session_list(
            expected_sessions
                .iter()
                .map(|s| clone_session_entry(s))
                .collect::<Result<Vec<SessionEntry>, _>>()
                .expect("Clone of expected sessions")
        ));
    }

    // Remove sessions in an odd pattern and ensure the list remains accurate.
    let mut removed = 0;
    for i in 0..(count / 2 - 1) {
        test_sessions.swap_remove(i * 2 - removed);
        let entry = expected_sessions.swap_remove(i * 2 - removed);
        removed += 1;
        // We wait for the removal event first to ensure the server sees the disconnect.
        await!(test_service.expect_sessions_change(SessionsChange {
            session: entry,
            delta: SessionDelta::Removed
        }));
        await!(test_service.expect_session_list(
            expected_sessions
                .iter()
                .map(|s| clone_session_entry(s))
                .collect::<Result<Vec<SessionEntry>, _>>()
                .expect("Clone of expected sessions")
        ));
        test_service.notify_events_handled();
    }
}
