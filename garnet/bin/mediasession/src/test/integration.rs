// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_mediaplayer::TimelineFunction;
use fidl_fuchsia_mediasession::{
    ActiveSession, ControllerControlHandle, ControllerEvent, ControllerMarker,
    ControllerRegistryEvent, ControllerRegistryEventStream, ControllerRegistryMarker,
    ControllerRegistryProxy, ControllerRequest, ControllerRequestStream, PlaybackState,
    PlaybackStatus, PublisherMarker, PublisherProxy, RepeatMode,
};
use fuchsia_app as app;
use fuchsia_async as fasync;
use futures::stream::{FusedStream, TryStreamExt};
use std::collections::HashMap;

const MEDIASESSION_URL: &str = "fuchsia-pkg://fuchsia.com/mediasession#meta/mediasession.cmx";

fn default_playback_status() -> PlaybackStatus {
    PlaybackStatus {
        duration: 100,
        playback_state: PlaybackState::Playing,
        playback_function: TimelineFunction {
            subject_time: 0,
            reference_time: 0,
            subject_delta: 1,
            reference_delta: 1,
        },
        repeat_mode: RepeatMode::Off,
        shuffle_on: false,
        has_next_item: true,
        has_prev_item: false,
        error: None,
    }
}

struct TestSession {
    request_stream: ControllerRequestStream,
    control_handle: ControllerControlHandle,
    client_end: ClientEnd<ControllerMarker>,
}

impl TestSession {
    fn new() -> Result<Self, Error> {
        let (client_end, server_end) =
            create_endpoints::<ControllerMarker>().expect("Fidl endpoints.");

        let (request_stream, control_handle) = server_end
            .into_stream_and_control_handle()
            .context("Unpacking Controller server end.")?;

        Ok(Self { request_stream, control_handle, client_end })
    }
}

struct TestService {
    // This needs to stay alive to keep the service running.
    #[allow(unused)]
    app: app::client::App,
    publisher: PublisherProxy,
    controller_registry: ControllerRegistryProxy,
    active_session_changes: ControllerRegistryEventStream,
}

impl TestService {
    fn new() -> Result<Self, Error> {
        let launcher = app::client::Launcher::new().context("Creating launcher.")?;
        let mediasession = launcher
            .launch(String::from(MEDIASESSION_URL), None)
            .context("Launching mediasession.")?;

        let publisher =
            mediasession.connect_to_service(PublisherMarker).context("Connecting to Publisher.")?;
        let controller_registry = mediasession
            .connect_to_service(ControllerRegistryMarker)
            .context("Connecting to ControllerRegistry.")?;
        let active_session_changes = controller_registry.take_event_stream();

        Ok(Self { app: mediasession, publisher, controller_registry, active_session_changes })
    }

    async fn expect_active_session(&mut self, expected: Option<u64>) {
        assert!(!self.active_session_changes.is_terminated());
        let ControllerRegistryEvent::OnActiveSession { active_session: actual } =
            await!(self.active_session_changes.try_next())
                .expect("Reported active session.")
                .expect("Active session stream");
        assert_eq!(actual, expected.map(|session_id| Box::new(ActiveSession { session_id })));
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_reports_no_active_session() {
    let mut test_service = TestService::new().expect("Test service.");
    await!(test_service.expect_active_session(None));
}

#[fasync::run_singlethreaded]
#[test]
async fn service_routes_controls() {
    let test_service = TestService::new().expect("Test service.");

    // Creates a new session and publishes it. Returns the proxy through Media
    // Session service and the request stream on the backend.
    let new_session = || {
        async {
            let test_session = TestSession::new().expect("Test session.");
            let session_id = await!(test_service.publisher.publish(test_session.client_end))
                .expect("Session id.");
            let (client_end, server_end) =
                create_endpoints::<ControllerMarker>().expect("Controller endpoints.");
            test_service
                .controller_registry
                .connect_to_controller_by_id(session_id, server_end)
                .expect("To connect to session.");
            let proxy = client_end.into_proxy().expect("Controller a proxy.");
            (proxy, test_session.request_stream)
        }
    };

    let (proxy_a, mut request_stream_a) = await!(new_session());
    let (proxy_b, mut request_stream_b) = await!(new_session());

    proxy_a.play().expect("To call Play() on Session a.");
    proxy_b.pause().expect("To call Pause() on Session b.");

    let a_event = await!(request_stream_a.try_next()).expect("Next request from session a.");
    let b_event = await!(request_stream_b.try_next()).expect("Next request from session b.");

    assert!(match a_event {
        Some(ControllerRequest::Play { .. }) => true,
        _ => false,
    },);

    assert!(match b_event {
        Some(ControllerRequest::Pause { .. }) => true,
        _ => false,
    },);

    // Ensure the behaviour continues.

    proxy_b.play().expect("To call Play() on Session b.");

    let b_event = await!(request_stream_b.try_next()).expect("Next request from session b.");

    assert!(match b_event {
        Some(ControllerRequest::Play { .. }) => true,
        _ => false,
    },);
}

#[fasync::run_singlethreaded]
#[test]
async fn service_reports_published_active_session() {
    let mut test_service = TestService::new().expect("Test service.");
    await!(test_service.expect_active_session(None));

    let test_session = TestSession::new().expect("Test session.");
    let our_session_id =
        await!(test_service.publisher.publish(test_session.client_end)).expect("Session id.");
    test_session
        .control_handle
        .send_on_playback_status_changed(&mut default_playback_status())
        .expect("To update playback status.");
    await!(test_service.expect_active_session(Some(our_session_id)));
}

#[fasync::run_singlethreaded]
#[test]
async fn service_reports_changed_active_session() {
    let mut test_service = TestService::new().expect("Test service.");
    await!(test_service.expect_active_session(None));

    // Publish sessions.
    let session_count: usize = 100;
    let mut keep_alive = Vec::new();
    for i in 0..session_count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("Session {}", i));
        test_session
            .control_handle
            .send_on_playback_status_changed(&mut default_playback_status())
            .expect("To update playback status.");
        await!(test_service.expect_active_session(Some(session_id)));
        keep_alive.push(test_session.control_handle);
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_broadcasts_events() {
    let mut test_service = TestService::new().expect("Test service.");
    await!(test_service.expect_active_session(None));

    let test_session = TestSession::new().expect("Test session.");
    let session_id = await!(test_service.publisher.publish(test_session.client_end))
        .expect(&format!("To publish session."));

    // Send a single event.
    test_session
        .control_handle
        .send_on_playback_status_changed(&mut default_playback_status())
        .expect("To update playback status.");

    // Ensure we wait for the service to accept the session.
    await!(test_service.expect_active_session(Some(session_id)));

    // Connect many clients and ensure they all receive the event.
    let client_count: usize = 100;
    for _ in 0..client_count {
        let (client_end, server_end) =
            create_endpoints::<ControllerMarker>().expect("Controller endpoints.");
        test_service
            .controller_registry
            .connect_to_controller_by_id(session_id, server_end)
            .expect("To connect to session.");
        let mut event_stream =
            client_end.into_proxy().expect("Controller proxy").take_event_stream();
        let event = await!(event_stream.try_next()).expect("Next Controller event.");
        assert_eq!(
            event.and_then(|event| match event {
                ControllerEvent::OnPlaybackStatusChanged { playback_status } => {
                    Some(playback_status)
                }
                _ => None,
            }),
            Some(default_playback_status())
        );
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn service_correctly_tracks_session_ids_states_and_lifetimes() {
    let test_service = TestService::new().expect("Test service.");

    // Publish 100 sessions and have each of them post a playback status.
    let count = 100;
    let mut test_sessions = Vec::new();
    let numbered_playback_status = |i| PlaybackStatus { duration: i, ..default_playback_status() };
    for i in 0..count {
        let test_session = TestSession::new().expect(&format!("Test session {}.", i));
        let session_id = await!(test_service.publisher.publish(test_session.client_end))
            .expect(&format!("To publish test session {}.", i));
        test_session
            .control_handle
            .send_on_playback_status_changed(&mut numbered_playback_status(i as i64))
            .expect(&format!("To broadcast playback status {}.", i));
        test_sessions.push((session_id, test_session.control_handle));
    }

    enum Expectation {
        SessionIsDropped,
        SessionReportsPlaybackStatus(PlaybackStatus),
    }

    // Set up expectations.
    let mut expectations = HashMap::new();
    let mut control_handles_to_keep_sessions_alive = Vec::new();
    for (i, (session_id, control_handle)) in test_sessions.into_iter().enumerate() {
        let should_drop = i % 3 == 0;
        expectations.insert(
            session_id,
            if should_drop {
                control_handle.shutdown();
                Expectation::SessionIsDropped
            } else {
                control_handles_to_keep_sessions_alive.push(control_handle);
                Expectation::SessionReportsPlaybackStatus(numbered_playback_status(i as i64))
            },
        );
    }

    // Check all expectations.
    for (session_id, expectation) in expectations.into_iter() {
        let (client_end, server_end) =
            create_endpoints::<ControllerMarker>().expect("Fidl endpoints.");
        test_service
            .controller_registry
            .connect_to_controller_by_id(session_id, server_end)
            .expect(&format!("To make connection request to session {}", session_id));
        let mut event_stream = client_end
            .into_proxy()
            .expect(&format!("Controller proxy for session {}.", session_id))
            .take_event_stream();
        let maybe_event = await!(event_stream.try_next()).expect("Next session event.");
        match expectation {
            Expectation::SessionIsDropped => {
                // If we shutdown the session, this or the next event should be
                // None depending on whether our shutdown reached the service
                // before this request.
                if maybe_event.is_some() {
                    let next_event = await!(event_stream.try_next()).expect("Next session event.");
                    assert!(next_event.is_none(), "{:?}", next_event)
                }
            }
            Expectation::SessionReportsPlaybackStatus(expected) => match maybe_event {
                Some(ControllerEvent::OnPlaybackStatusChanged { playback_status: actual }) => {
                    assert_eq!(actual, expected)
                }
                other => panic!("Expected a playback status event; got: {:?}", other),
            },
        }
    }
}
